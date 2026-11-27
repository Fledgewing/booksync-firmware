// See Tailnet.h. One worker task owns the MicroLink handle; WiFi event
// callbacks only post requests to it, because microlink_stop() can block for
// up to CONFIG_ML_STOP_TIMEOUT_MS and must not run on Arduino's event task.
#include "Tailnet.h"

#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lwip/dns.h>
#include <lwip/tcpip.h>
#include <nvs.h>
#include <stdio.h>
#include <string.h>

#include <atomic>

extern "C" {
#include "TailnetNetPhase.h"
#include "TailnetSysSwitches.h"
#include <esp_timer.h>
#include "microlink.h"
void tailnet_ml_set_priority_peer(microlink_t* ml, uint32_t vpn_ip);     // ml_priority.c
void tailnet_ml_relay_codes(const microlink_t* ml, uint32_t vpn_ip, char* home, char* peer, size_t len);
void tailnet_ml_set_forced_region(microlink_t* ml, uint16_t region);
void tailnet_ml_request_handshake(microlink_t* ml, uint32_t vpn_ip);
bool tailnet_ml_tunnel_up(microlink_t* ml, uint32_t vpn_ip);
void tailnet_ml_path(const microlink_t* ml, uint32_t vpn_ip, char* out, size_t len);
uint16_t tailnet_ml_home_region(const microlink_t* ml);
uint16_t tailnet_ml_peer_region(const microlink_t* ml, uint32_t vpn_ip);
int tailnet_ml_regions(const microlink_t* ml, uint16_t* ids, char (*codes)[8], int max);
}

// Arduino's esp32-hal-log.h (pulled in by WiFi.h) turns ESP_LOGx into its own
// log_x macros, which CORE_DEBUG_LEVEL compiles out. Write to the ESP-IDF log
// directly so TailnetLog.cpp captures these lines.
#define TLOG(level, letter, fmt, ...) \
  tailnet_log_emit(letter " (%lu) %s: " fmt "\n", (unsigned long)esp_log_timestamp(), TAG, ##__VA_ARGS__)
#define TLOGE(fmt, ...) TLOG(ESP_LOG_ERROR, "E", fmt, ##__VA_ARGS__)
#define TLOGW(fmt, ...) TLOG(ESP_LOG_WARN, "W", fmt, ##__VA_ARGS__)
#define TLOGI(fmt, ...) TLOG(ESP_LOG_INFO, "I", fmt, ##__VA_ARGS__)

namespace {

const char* TAG = "tailnet";
const char* NVS_NS = "tailnet";
const char* NVS_KEY_ENABLED = "enabled";
const char* NVS_KEY_JOINED = "joined";  // set once connected; cleared by logout
const char* NVS_KEY_PIN = "pin_ip";     // learned priority peer (host byte order)
const char* NVS_KEY_RELAY_MODE = "relay_mode";  // tailnet_relay_mode_t
const char* NVS_KEY_RELAY_FIXED = "relay_rgn";  // region picked by the user
const char* NVS_KEY_SERVER_RGN = "srv_rgn";     // learned home region of the server
const char* NVS_KEY_SERVER_IP = "srv_ip";       // tailnet address last connected to

enum Request : uint8_t { REQ_WIFI_UP, REQ_WIFI_DOWN, REQ_RECHECK, REQ_LOGOUT, REQ_REPIN, REQ_RELAY, REQ_REFRESH };

QueueHandle_t s_queue = nullptr;
microlink_t* s_ml = nullptr;  // owned by the worker task
bool s_running = false;       // worker task only
// MicroLink's tasks are alive: set once microlink_start() succeeds, cleared once
// microlink_stop() has returned. Read by tailnet_stop_before_wifi_off() on other tasks.
std::atomic<bool> s_ml_live{false};
bool s_wifi_up = false;       // worker task only

std::atomic<int> s_status{TAILNET_OFF};
std::atomic<uint32_t> s_generation{0};
std::atomic<uint32_t> s_ip{0};
std::atomic<int> s_enabled{-1};  // -1 = not loaded from NVS yet
std::atomic<int> s_joined{-1};   // -1 = not loaded from NVS yet
std::atomic<uint32_t> s_pin{0};  // priority peer; loaded once at the first start
bool s_pin_loaded = false;       // worker task only
// Peers already re-pinned this WiFi session (no restart loops for a peer that
// is offline or not on the tailnet). Written by callers of tailnet_ensure_peer.
portMUX_TYPE s_tried_lock = portMUX_INITIALIZER_UNLOCKED;
uint32_t s_tried[4];
// One-shot control (MicroLink's ML_LITE_CONTROL_ONESHOT): the peer whose
// failed tunnel already got a fresh map this WiFi session (0 = none).
std::atomic<uint32_t> s_tunnel_refreshed{0};

// The login URL is written by MicroLink's task (via the state callback) and
// read by UI tasks.
portMUX_TYPE s_url_lock = portMUX_INITIALIZER_UNLOCKED;
char s_url[MICROLINK_AUTH_URL_MAX];
static_assert(TAILNET_LOGIN_URL_MAX >= MICROLINK_AUTH_URL_MAX, "Tailnet.h buffer too small");

// Relay settings, cached; written by the UI task (setter) and the worker.
std::atomic<int> s_relay_mode{-1};      // -1 = not loaded
std::atomic<int> s_relay_fixed{0};
std::atomic<int> s_server_rgn{0};
std::atomic<uint32_t> s_server_ip{0};
bool s_relay_matched = false;  // worker only: one match-server restart per WiFi session

#if CONFIG_ML_LITE
// Lite builds (the C3: no PSRAM) start MicroLink only in boots that hand it
// the framebuffer's memory (TailnetNetPhase.h): tailnet_permit_start().
std::atomic<bool> s_permit{false};
std::atomic<bool> s_register_only{false};  // the login boot: register, nothing more
std::atomic<bool> s_on_demand{false};      // start at the first tailnet name or address, not at GOT_IP
std::atomic<bool> s_unparked{false};       // catalog probe: run with the framebuffer kept
// Live catalog connect (tailnet_catalog_connect()): the worker's decision at start.
std::atomic<int> s_catalog_mode{TAILNET_CATALOG_NONE};
std::atomic<int> s_catalog_result{TAILNET_CATALOG_PENDING};
std::atomic<bool> s_low_memory{false};     // stopped for memory this WiFi session (written by the worker)
// Below this much free internal RAM MicroLink is stopped: WiFi, lwIP and DNS
// stop working well before 0, and a session would only spin retries.
constexpr size_t LOW_MEMORY_BYTES = 8 * 1024;
// When free RAM first went below LOW_MEMORY_BYTES (0 = not below); worker task only.
uint32_t s_low_since = 0;
const char* NVS_KEY_LOGIN_URL = "login_url";  // the last login URL, for the login boot's QR
#endif

uint8_t loadFlag(const char* key, std::atomic<int>& cache);
void storeFlag(const char* key, std::atomic<int>& cache, bool value);

void setStatus(tailnet_status_t st) {
  if (s_status.exchange(st) != st) s_generation.fetch_add(1);
}

#if CONFIG_ML_LITE
// "" erases it.
void saveLoginUrl(const char* url) {
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
  if (url[0]) {
    nvs_set_str(h, NVS_KEY_LOGIN_URL, url);
  } else {
    nvs_erase_key(h, NVS_KEY_LOGIN_URL);
  }
  nvs_commit(h);
  nvs_close(h);
}
#endif

#if TAILNET_LAZY_WORKER
void workerTask(void*);
// The worker exists only while it has work (TAILNET_LAZY_WORKER): most boots
// never run Tailscale, and its stack is TAILNET_WORKER_STACK bytes of heap.
SemaphoreHandle_t s_worker_lock = nullptr;  // guards s_worker and its exit
TaskHandle_t s_worker = nullptr;
std::atomic<bool> s_link_up{false};  // WiFi has an address (set by the event callback)
bool loadEnabled();

// Would a worker have anything to do with r? Without one, the state it would
// track is rebuilt from s_link_up when one starts.
bool workerWanted(Request r) {
  if (!tailnet_service_permitted()) return false;
  if (r == REQ_LOGOUT) return true;
#if CONFIG_ML_LITE
  return s_link_up.load() && s_permit.load() && loadEnabled();
#else
  return s_link_up.load() && loadEnabled();
#endif
}

void post(Request r) {
  if (!s_queue || !s_worker_lock) return;
  xSemaphoreTake(s_worker_lock, portMAX_DELAY);
  // xTaskCreate uses ESP-IDF's internal-RAM stack allocator. Never replace it
  // with an external-capability/static PSRAM stack: this worker writes NVS.
  const size_t freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t largestInternal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!s_worker && workerWanted(r) &&
      (freeInternal < TAILNET_WORKER_STACK + 12 * 1024 || largestInternal < TAILNET_WORKER_STACK + 8 * 1024)) {
    xSemaphoreGive(s_worker_lock);
    TLOGE("Worker deferred: internal free=%u largest=%u stack=%u", (unsigned)freeInternal,
          (unsigned)largestInternal, (unsigned)TAILNET_WORKER_STACK);
    return;
  }
  if (!s_worker && workerWanted(r) &&
      xTaskCreate(workerTask, "tailnet", TAILNET_WORKER_STACK, nullptr, 3, &s_worker) != pdPASS) {
    s_worker = nullptr;
    xSemaphoreGive(s_worker_lock);
    TLOGE("No memory for the worker task (%u bytes; heap free %u)", (unsigned)TAILNET_WORKER_STACK,
          (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    return;
  }
  if (s_worker) xQueueSend(s_queue, &r, 0);
  xSemaphoreGive(s_worker_lock);
}
#else
void post(Request r) {
  if (s_queue) xQueueSend(s_queue, &r, 0);
}
#endif

tailnet_status_t mapState(microlink_state_t st) {
  switch (st) {
    case ML_STATE_CONNECTED:
      return TAILNET_CONNECTED;
    case ML_STATE_RECONNECTING:
      return TAILNET_RECONNECTING;
    case ML_STATE_NEEDS_LOGIN:
      return TAILNET_NEEDS_LOGIN;
    case ML_STATE_AUTH_FAILED:
    case ML_STATE_ERROR:
      return TAILNET_FAILED;
    case ML_STATE_IDLE:
      return TAILNET_OFF;
    default:
      return TAILNET_STARTING;
  }
}

// MicroLink only reports CONNECTED, RECONNECTING and AUTH_FAILED through the
// callback, so the worker also polls while running (see workerTask).
void refreshFromHandle() {
  if (!s_ml || !s_running) return;
  const microlink_state_t st = microlink_get_state(s_ml);
  if (st == ML_STATE_NEEDS_LOGIN) {
    char url[MICROLINK_AUTH_URL_MAX] = {};
    if (microlink_get_auth_url(s_ml, url, sizeof(url)) == ESP_OK && url[0]) {
      bool changed;
      portENTER_CRITICAL(&s_url_lock);
      changed = strcmp(url, s_url) != 0;
      memcpy(s_url, url, sizeof(s_url));
      portEXIT_CRITICAL(&s_url_lock);
      if (changed) {
        TLOGW("Tailscale login needed: %s", url);
        s_generation.fetch_add(1);
#if CONFIG_ML_LITE
        saveLoginUrl(url);
#endif
      }
    }
  }
  s_ip.store(st == ML_STATE_CONNECTED ? microlink_get_vpn_ip(s_ml) : 0);
  if (st == ML_STATE_CONNECTED && !loadFlag(NVS_KEY_JOINED, s_joined)) {
    storeFlag(NVS_KEY_JOINED, s_joined, true);
#if CONFIG_ML_LITE
    saveLoginUrl("");
#endif
  }
  setStatus(mapState(st));
}

void onState(microlink_t*, microlink_state_t, void*) { post(REQ_RECHECK); }

uint8_t loadFlag(const char* key, std::atomic<int>& cache) {
  int cached = cache.load();
  if (cached >= 0) return (uint8_t)cached;
  uint8_t v = 0;
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
    nvs_get_u8(h, key, &v);
    nvs_close(h);
  }
  cache.store(v ? 1 : 0);
  return v ? 1 : 0;
}

void storeFlag(const char* key, std::atomic<int>& cache, bool value) {
  if (loadFlag(key, cache) == (value ? 1 : 0)) return;
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
    nvs_set_u8(h, key, value ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
  }
  cache.store(value ? 1 : 0);
  s_generation.fetch_add(1);
}

// Off until turned on in Settings > Tailscale.
bool loadEnabled() { return loadFlag(NVS_KEY_ENABLED, s_enabled) != 0; }

void stopMl() {
  if (!s_running) return;
  // Retry: stop returns ESP_ERR_TIMEOUT (and frees nothing) if a task is
  // stuck in a blocking call; a later call resumes the wait.
  for (int i = 0; i < 4; i++) {
    if (microlink_stop(s_ml) == ESP_OK) break;
    TLOGW("microlink_stop timed out; retrying");
  }
  s_running = false;
  s_ml_live = false;
  s_ip.store(0);
  portENTER_CRITICAL(&s_url_lock);
  s_url[0] = '\0';
  portEXIT_CRITICAL(&s_url_lock);
  setStatus(TAILNET_OFF);
}

uint32_t nvsGetU32(const char* key, uint32_t missing = 0) {
  uint32_t v = missing;
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
    nvs_get_u32(h, key, &v);
    nvs_close(h);
  }
  return v;
}

void nvsSetU32(const char* key, uint32_t v) {
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
    nvs_set_u32(h, key, v);
    nvs_commit(h);
    nvs_close(h);
  }
}

void loadRelay() {
  if (s_relay_mode.load() >= 0) return;
  s_relay_fixed.store((int)nvsGetU32(NVS_KEY_RELAY_FIXED));
  s_server_rgn.store((int)nvsGetU32(NVS_KEY_SERVER_RGN));
  s_server_ip.store(nvsGetU32(NVS_KEY_SERVER_IP));
  // Default: match the server. Both ends then share one relay region without
  // a second relay connection, which the readers' internal RAM can't spare.
  uint32_t m = nvsGetU32(NVS_KEY_RELAY_MODE, TAILNET_RELAY_MATCH_SERVER);
  s_relay_mode.store(m <= TAILNET_RELAY_FIXED ? (int)m : TAILNET_RELAY_MATCH_SERVER);
}

// The home region to force at start (0 = let MicroLink pick the closest).
uint16_t forcedRegion() {
  loadRelay();
  switch (s_relay_mode.load()) {
    case TAILNET_RELAY_FIXED:
      return (uint16_t)s_relay_fixed.load();
    case TAILNET_RELAY_MATCH_SERVER:
      return (uint16_t)s_server_rgn.load();  // 0 until learned: closest for now
    default:
      return 0;
  }
}

uint32_t serverIp() {
  loadRelay();
  uint32_t ip = s_server_ip.load();
  return ip ? ip : s_pin.load();
}

// Match-server mode: once the server is in the peer table, follow its home
// region if ours differs (remembered, so later starts use it right away).
void matchServerRegion() {
  if (!s_ml || s_relay_matched || s_relay_mode.load() != TAILNET_RELAY_MATCH_SERVER) return;
#if CONFIG_ML_LITE
  // The restart would run a control session; the catalog session (framebuffer
  // kept) has no room for one. The next network boot follows the server.
  if (s_unparked.load()) return;
#endif
  const uint16_t theirs = tailnet_ml_peer_region(s_ml, serverIp());
  const uint16_t ours = tailnet_ml_home_region(s_ml);
  if (!theirs || !ours || theirs == ours) return;
  s_relay_matched = true;
  TLOGW("Relay region %u -> %u to match the server; restarting MicroLink", ours, theirs);
  nvsSetU32(NVS_KEY_SERVER_RGN, theirs);
  s_server_rgn.store(theirs);
  stopMl();  // reconcile() starts it again with the new region
}

uint32_t loadPin() {
  if (!s_pin_loaded) {
    uint32_t v = 0;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
      nvs_get_u32(h, NVS_KEY_PIN, &v);
      nvs_close(h);
    }
    s_pin.store(v);
    s_pin_loaded = true;
  }
  return s_pin.load();
}

void storePin(uint32_t ip) {
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
    nvs_set_u32(h, NVS_KEY_PIN, ip);
    nvs_commit(h);
    nvs_close(h);
  }
  s_pin.store(ip);
  s_pin_loaded = true;
}

bool initMl() {
  if (s_ml) return true;
  microlink_config_t cfg = {};
  cfg.auth_key = nullptr;     // interactive login; nothing secret in the image
  cfg.device_name = nullptr;  // esp32-<mac>; rename it in the admin console
  s_ml = microlink_init(&cfg);
  if (!s_ml) {
    TLOGE("microlink_init failed (free heap %u)", (unsigned)esp_get_free_heap_size());
    setStatus(TAILNET_FAILED);
    return false;
  }
  microlink_set_state_callback(s_ml, onState, nullptr);
  return true;
}

// Settings applied before every start. The control plane only resends a
// dropped peer on a fresh map, so the pin is (re)applied each time.
void prepareMl(bool control_fallback) {
  tailnet_ml_set_priority_peer(s_ml, loadPin());
  tailnet_ml_set_forced_region(s_ml, forcedRegion());
#if CONFIG_ML_LITE
  microlink_control_register_only(s_register_only.load());
  // Skip the control session when the state the last one left is still good
  // (MicroLink's ML_LITE_CACHED_START): it needs the server this reader talks
  // to in the peer cache. A control session as fallback may start by itself
  // only with the framebuffer parked: with it kept (catalog) there is no room.
  microlink_cached_start_peer(s_ml, serverIp(), control_fallback);
  // ML_LITE_NO_DIRECT 2 (the X3): a parked boot (download, sync, login, Home's
  // refresh) has the room for the direct path's UDP socket, net_io task and
  // DISCO; with the framebuffer kept (the live catalog) it stays DERP only.
  // Where no direct path forms, traffic stays on DERP.
  microlink_set_direct_path(s_ml, control_fallback);
#else
  (void)control_fallback;
#endif
}

#if CONFIG_ML_LITE
// A start with the framebuffer kept (the live catalog, the catalog probe), or
// a live catalog connect that may park it. Never a control session with the
// framebuffer kept: without usable cached state, the catalog connect parks
// (TAILNET_CATALOG_ALLOW_CONTROL) and anything else does not start.
bool startKeptOrCatalog(int catalog) {
  if (!initMl()) {
    if (catalog) s_catalog_result = TAILNET_CATALOG_FAILED;
    s_permit = false;
    return false;
  }
  prepareMl(false);
  if (microlink_control_cache_usable(s_ml)) {
    s_unparked = true;
    if (catalog) s_catalog_result = TAILNET_CATALOG_CACHED;
    TLOGI("Starting on cached state with the framebuffer kept (internal heap free %u, largest %u)",
          (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    return true;
  }
  if (catalog == TAILNET_CATALOG_ALLOW_CONTROL && !s_unparked.load() && tailnet_display_park &&
      tailnet_display_park()) {
    s_catalog_result = TAILNET_CATALOG_CONTROL;
    TLOGI("No usable cached state: control session with the framebuffer parked (internal heap free %u, largest %u)",
          (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    return true;
  }
  TLOGW("Not starting: no usable cached state, and no control session with the framebuffer kept");
  if (catalog) s_catalog_result = TAILNET_CATALOG_REFUSED;
  s_permit = false;  // no retry this boot: reconcile() would ask again every second
  setStatus(TAILNET_FAILED);
  return false;
}
#endif

void startMl() {
  if (s_running || !tailnet_service_permitted()) return;
#if CONFIG_ML_LITE
  const int catalog = s_catalog_mode.exchange(TAILNET_CATALOG_NONE);
  if (catalog != TAILNET_CATALOG_NONE || s_unparked.load()) {
    if (!startKeptOrCatalog(catalog)) return;
  } else {
    // TailnetNetPhase.h: hand the display's framebuffer to the heap first. The
    // C3 cannot hold both, so no framebuffer parked, no MicroLink.
    if (!tailnet_display_park || !tailnet_display_park()) {
      TLOGW("Not starting: the display framebuffer could not be parked");
      return;
    }
    TLOGI("Starting%s (internal heap free %u, largest %u)", s_register_only.load() ? " for login" : "",
          (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    if (!initMl()) return;
    prepareMl(true);
  }
#else
  if (!initMl()) return;
  prepareMl(true);
#endif
  setStatus(TAILNET_STARTING);
  const esp_err_t err = microlink_start(s_ml);
  if (err != ESP_OK) {
    TLOGE("microlink_start: %s", esp_err_to_name(err));
    setStatus(TAILNET_FAILED);
    return;
  }
  s_running = true;
  s_ml_live = true;
  TLOGI("MicroLink started");
}

void reconcile() {
#if CONFIG_ML_LITE
  const bool want = tailnet_service_permitted() && s_wifi_up && loadEnabled() && s_permit.load() && !s_low_memory.load();
#else
  const bool want = tailnet_service_permitted() && s_wifi_up && loadEnabled();
#endif
  if (want && !s_running) startMl();
  if (!want && s_running) stopMl();
#if CONFIG_ML_LITE
  // A start made while parked (TAILNET_PARK_DURING_CONNECT) allowed MicroLink's
  // own control fallback. Once the framebuffer is back (tailnet_probe_permit_unparked
  // posts a recheck), a late cached-relay failure must not start a control session.
  if (s_ml && s_unparked.load()) microlink_cached_start_peer(s_ml, serverIp(), false);
  if (!s_wifi_up || !tailnet_service_permitted()) {
    // Free the context too (microlink_init again on the next start), so the
    // display can get its framebuffer back in one block.
    if (s_ml && !s_running) {
      microlink_destroy(s_ml);
      s_ml = nullptr;
    }
    s_low_memory = false;
    if (tailnet_display_unpark) tailnet_display_unpark();
  }
#endif
}

void workerTask(void*) {
#if TAILNET_LAZY_WORKER
  // Requests that came while no worker ran were not queued: start from the
  // current WiFi state, and a fresh WiFi session's bookkeeping.
  s_wifi_up = s_link_up.load();
  s_relay_matched = false;
  portENTER_CRITICAL(&s_tried_lock);
  memset(s_tried, 0, sizeof(s_tried));
  portEXIT_CRITICAL(&s_tried_lock);
  s_tunnel_refreshed.store(0);
#endif
  for (;;) {
    Request r;
#if TAILNET_LAZY_WORKER
    // Nothing running and nothing held: leave. post() starts a new worker
    // when a request needs one.
    if (!s_running && !s_ml) {
      xSemaphoreTake(s_worker_lock, portMAX_DELAY);
      if (uxQueueMessagesWaiting(s_queue) == 0) {
        TLOGI("Worker idle; exiting (stack: %u of %u bytes never used)", (unsigned)uxTaskGetStackHighWaterMark(nullptr),
              (unsigned)TAILNET_WORKER_STACK);
        s_worker = nullptr;
        xSemaphoreGive(s_worker_lock);
        vTaskDelete(nullptr);
      }
      xSemaphoreGive(s_worker_lock);
    }
#endif
    // While running, wake every second to pick up states MicroLink does not
    // report through its callback (NEEDS_LOGIN, the URL, the VPN address).
    const TickType_t wait = s_running ? pdMS_TO_TICKS(1000) : portMAX_DELAY;
    if (xQueueReceive(s_queue, &r, wait) == pdTRUE) {
      switch (r) {
        case REQ_WIFI_UP:
          s_wifi_up = true;
          break;
        case REQ_WIFI_DOWN:
          s_wifi_up = false;
          s_relay_matched = false;
          portENTER_CRITICAL(&s_tried_lock);
          memset(s_tried, 0, sizeof(s_tried));
          portEXIT_CRITICAL(&s_tried_lock);
          s_tunnel_refreshed.store(0);
          break;
        case REQ_LOGOUT:
          stopMl();
          if (s_ml) {
            microlink_destroy(s_ml);
            s_ml = nullptr;
          }
          microlink_factory_reset();
          storeFlag(NVS_KEY_JOINED, s_joined, false);
          TLOGW("Tailnet identity erased");
          break;
        case REQ_REPIN:
          // A peer the reader needs is not in MicroLink's table (a large
          // tailnet fills it). Restart with it pinned: the fresh map lets it
          // evict the least recently used peer. Keys and login are kept.
          if (s_running) {
            TLOGW("Pinning %u.%u.%u.%u as priority peer; restarting MicroLink", (unsigned)(s_pin >> 24) & 0xff,
                  (unsigned)(s_pin >> 16) & 0xff, (unsigned)(s_pin >> 8) & 0xff, (unsigned)s_pin & 0xff);
            stopMl();
          }
          break;
        case REQ_RELAY:
          if (s_running) {
            TLOGW("Relay setting changed; restarting MicroLink");
            stopMl();
          }
          s_relay_matched = false;
          break;
        case REQ_REFRESH:
          // One-shot control: fetch a fresh map with the (new) pin, without
          // restarting MicroLink. The tunnel stays up meanwhile.
          if (s_running && s_ml) {
            tailnet_ml_set_priority_peer(s_ml, loadPin());
            const esp_err_t err = microlink_control_refresh(s_ml);
            if (err != ESP_OK) TLOGW("control refresh: %s", esp_err_to_name(err));
          }
          break;
        case REQ_RECHECK:
          break;
      }
      reconcile();
    }
#if CONFIG_ML_LITE
    if (s_running) {
      const size_t free_now = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
      const uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
      if (free_now >= LOW_MEMORY_BYTES) {
        if (s_low_since) TLOGI("Low memory over: %u bytes free", (unsigned)free_now);
        s_low_since = 0;
      } else if (!s_low_since) {
        // A request's handshake or JSON parse dips for a moment and gives it back:
        // stop only if it stays low (TAILNET_LOW_MEMORY_HOLD_MS).
        s_low_since = now_ms | 1u;
        TLOGW("Low memory: %u bytes free (lowest %u); stopping Tailscale if it lasts %u ms", (unsigned)free_now,
              (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL), (unsigned)TAILNET_LOW_MEMORY_HOLD_MS);
      }
      if (s_low_since && now_ms - s_low_since >= TAILNET_LOW_MEMORY_HOLD_MS) {
        s_low_since = 0;
        TLOGE("Low memory: %u bytes free (lowest %u); stopping Tailscale until WiFi reconnects", (unsigned)free_now,
              (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
        s_low_memory = true;
        stopMl();
        setStatus(TAILNET_FAILED);
        continue;
      }
    } else {
      s_low_since = 0;
    }
#endif
    refreshFromHandle();
    if (s_running && tailnet_status() == TAILNET_CONNECTED) {
      matchServerRegion();
      if (!s_running) reconcile();
    }
  }
}

void onWifiEvent(arduino_event_id_t event) {
#if TAILNET_LAZY_WORKER
  s_link_up.store(event == ARDUINO_EVENT_WIFI_STA_GOT_IP);
#endif
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      // lwIP's DNS cache outlives a change of network: a name answered by
      // the home network's split DNS (a LAN address) would be reused on
      // another network until its TTL ran out. Start each network clean.
      tcpip_callback([](void*) { dns_clear_cache(); }, nullptr);
      post(REQ_WIFI_UP);
      break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    case ARDUINO_EVENT_WIFI_STA_STOP:
      post(REQ_WIFI_DOWN);
      break;
    default:
      break;
  }
}

}  // namespace

// Arduino's initArduino() calls initVariant() from app_main, before setup():
// the scheduler is running and every static object (Arduino's Network event
// list included) is constructed. The core's definition is weak and empty, and
// neither CrossInk, the FreeInk SDK nor the S3/C3 variants define one, so this
// strong definition is the hook. A future clash would fail the link loudly.
extern "C" void initVariant() {
  tailnet_log_install();
  // Requests outlive Wi-Fi callbacks and worker instances; FreeRTOS owns this
  // bounded eight-entry queue for the boot. A task stack cannot back it.
  s_queue = xQueueCreate(8, sizeof(Request));
  if (!s_queue) {
    TLOGE("No memory for Tailnet request queue");
    return;
  }
  // MicroLink's own tasks do the network work; this one only sequences
  // start/stop, but microlink_init() and NVS writes need some stack.
#if TAILNET_LAZY_WORKER
  s_worker_lock = xSemaphoreCreateMutex();  // shared across transient workers
  if (!s_worker_lock) {
    TLOGE("No memory for Tailnet worker mutex");
    vQueueDelete(s_queue);
    s_queue = nullptr;
    return;
  }
#else
  xTaskCreate(workerTask, "tailnet", TAILNET_WORKER_STACK, nullptr, 3, nullptr);
#endif
  WiFi.onEvent(onWifiEvent, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(onWifiEvent, ARDUINO_EVENT_WIFI_STA_LOST_IP);
  WiFi.onEvent(onWifiEvent, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.onEvent(onWifiEvent, ARDUINO_EVENT_WIFI_STA_STOP);
}

extern "C" void tailnet_service_changed(void) { post(REQ_RECHECK); }

extern "C" tailnet_status_t tailnet_status(void) { return (tailnet_status_t)s_status.load(); }

extern "C" bool tailnet_login_url(char* buf, size_t len) {
  if (!buf || len == 0) return false;
  portENTER_CRITICAL(&s_url_lock);
  strncpy(buf, s_url, len - 1);
  buf[len - 1] = '\0';
  portEXIT_CRITICAL(&s_url_lock);
  return buf[0] != '\0' && tailnet_status() == TAILNET_NEEDS_LOGIN;
}

extern "C" uint32_t tailnet_ip(void) { return s_ip.load(); }

extern "C" uint32_t tailnet_generation(void) { return s_generation.load(); }

extern "C" bool tailnet_enabled(void) { return loadEnabled(); }

extern "C" void tailnet_set_enabled(bool enabled) {
  storeFlag(NVS_KEY_ENABLED, s_enabled, enabled);
  post(REQ_RECHECK);
}

extern "C" bool tailnet_joined(void) { return loadFlag(NVS_KEY_JOINED, s_joined) != 0; }

extern "C" void tailnet_logout(void) { post(REQ_LOGOUT); }

extern "C" uint32_t tailnet_resolve(const char* name) {
  // Only meaningful while connected; the handle outlives stop/start, and
  // microlink_resolve reads the peer table it keeps.
  if (!s_ml || tailnet_status() != TAILNET_CONNECTED || !name) return 0;
  char q[128];
  size_t n = strnlen(name, sizeof(q) - 1);
  memcpy(q, name, n);
  q[n] = '\0';
  if (n && q[n - 1] == '.') q[n - 1] = '\0';  // "host.tailnet.ts.net."
  return microlink_resolve(s_ml, q);
}

namespace {
uint32_t elapsedMs(TickType_t start) { return (uint32_t)((xTaskGetTickCount() - start) * portTICK_PERIOD_MS); }

// Tasks that must never wait for Tailscale: MicroLink's own (ml_*), lwIP's
// (tiT), the worker that starts MicroLink, and the event tasks that feed it.
bool mayWaitForTailscale() {
  const char* name = pcTaskGetName(nullptr);
  if (!name) return true;
  return strncmp(name, "ml_", 3) != 0 && strcmp(name, "tiT") != 0 && strcmp(name, "tailnet") != 0 &&
         strcmp(name, "arduino_events") != 0 && strcmp(name, "sys_evt") != 0 && strcmp(name, "wifi") != 0 &&
         strcmp(name, "esp_timer") != 0;
}

// Tailscale is enabled and logged in and WiFi is up, but MicroLink has not been
// started yet (the worker starts it on GOT_IP): worth a short wait.
bool aboutToStart() {
#if CONFIG_ML_LITE
  if (!s_permit.load() || s_low_memory.load()) return false;
#endif
  return loadEnabled() && tailnet_joined() && WiFi.isConnected();
}
}  // namespace

extern "C" bool tailnet_wait_connected_for(const char* what, uint32_t timeout_ms) {
  const TickType_t start = xTaskGetTickCount();
  if (tailnet_status() == TAILNET_CONNECTED) return true;
  if (!mayWaitForTailscale()) return false;
  bool logged = false;
  for (;;) {
    const tailnet_status_t st = tailnet_status();
    const uint32_t waited = elapsedMs(start);
    if (st == TAILNET_CONNECTED) {
      if (logged) TLOGI("%s: Tailscale ready after %lu ms", what, (unsigned long)waited);
      return true;
    }
    // Waiting is worth it only for a start in progress (or one about to begin
    // right after GOT_IP, for up to 3 s); off, failed, logged out or stopped
    // (low memory, WiFi down) will not change within a request's timeout.
    const bool starting = st == TAILNET_STARTING || st == TAILNET_RECONNECTING;
    const bool soon = st == TAILNET_OFF && waited < 3000 && aboutToStart();
    if (!starting && !soon) {
      if (what) TLOGW("%s: Tailscale not up (status %d); failing at once", what, (int)st);
      return false;
    }
    if (waited >= timeout_ms) {
      if (what) TLOGW("%s: Tailscale still not ready after %lu ms (status %d)", what, (unsigned long)waited, (int)st);
      return false;
    }
    if (what && !logged) {
      logged = true;
      TLOGI("%s waiting for Tailscale (up to %lu ms)", what, (unsigned long)timeout_ms);
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

extern "C" bool tailnet_wait_connected(uint32_t timeout_ms) { return tailnet_wait_connected_for(nullptr, timeout_ms); }

namespace {
bool peerKnown(uint32_t ip) {
  if (!s_ml) return false;
  const int n = microlink_get_peer_count(s_ml);
  microlink_peer_info_t info;
  for (int i = 0; i < n; i++) {
    if (microlink_get_peer_info(s_ml, i, &info) == ESP_OK && info.vpn_ip == ip) return true;
  }
  return false;
}

// True the first time ip is seen this WiFi session.
bool markTried(uint32_t ip) {
  bool first = true;
  portENTER_CRITICAL(&s_tried_lock);
  int free_slot = -1;
  for (int i = 0; i < 4; i++) {
    if (s_tried[i] == ip) first = false;
    if (!s_tried[i] && free_slot < 0) free_slot = i;
  }
  if (first) s_tried[free_slot >= 0 ? free_slot : 0] = ip;
  portEXIT_CRITICAL(&s_tried_lock);
  return first;
}
}  // namespace

#define IP_ARGS(ip) (unsigned)((ip) >> 24) & 0xff, (unsigned)((ip) >> 16) & 0xff, (unsigned)((ip) >> 8) & 0xff, \
                    (unsigned)(ip) & 0xff

static std::atomic<uint32_t> s_last_target{0};

// The tailnet address the reader connects to is its library server (the only
// tailnet host CrossInk talks to); match-server mode follows its relay region.
static void rememberServer(uint32_t ip) {
  loadRelay();
  if (s_server_ip.exchange(ip) != ip) nvsSetU32(NVS_KEY_SERVER_IP, ip);
}

// Bring up the WireGuard session to a peer before the caller's TCP SYN goes
// out (see tailnet_ml_request_handshake). Re-asks every 2.5 s: a request made
// before the relay connection is up is dropped. A peer that never answers is
// left to the caller's own timeout.
static bool bringUpTunnel(uint32_t ip, uint32_t timeout_ms) {
  if (tailnet_ml_tunnel_up(s_ml, ip)) return true;
  const uint32_t limit = timeout_ms < 15000 ? timeout_ms : 15000;
  uint32_t waited = 0;
  tailnet_ml_request_handshake(s_ml, ip);
  while (!tailnet_ml_tunnel_up(s_ml, ip) && waited < limit && s_ml) {
    vTaskDelay(pdMS_TO_TICKS(250));
    waited += 250;
    if (waited % 2500 == 0) tailnet_ml_request_handshake(s_ml, ip);
  }
  if (tailnet_ml_tunnel_up(s_ml, ip)) {
    TLOGW("tunnel to %u.%u.%u.%u up after %lu ms", IP_ARGS(ip), (unsigned long)waited);
    return true;
  }
  TLOGW("tunnel to %u.%u.%u.%u: no WireGuard session after %lu ms", IP_ARGS(ip), (unsigned long)waited);
  return false;
}

// One-shot control: ask the worker for a fresh map (a new control session)
// and wait until one has succeeded. False if none did within timeout_ms.
static bool refreshControl(uint32_t timeout_ms, const char* why) {
  // State a cached start relied on is wrong: the next start runs a session.
  if (s_ml && microlink_control_cached(s_ml)) microlink_control_cache_invalidate(why);
#if CONFIG_ML_LITE
  // Never a control session in the catalog session (framebuffer kept): its
  // peak and the fragmentation it leaves break the requests that follow.
  if (s_unparked.load()) {
    TLOGW("Control fallback needed (%s); not with the framebuffer kept", why);
    return false;
  }
#endif
  TLOGW("Control fallback: %s", why);
  const uint32_t before = microlink_control_sessions();
  post(REQ_REFRESH);
  uint32_t waited = 0;
  while (waited < timeout_ms) {
    vTaskDelay(pdMS_TO_TICKS(100));
    waited += 100;
    if (microlink_control_sessions() != before) return true;
    // Nothing running a second after asking: the session gave up.
    if (waited >= 1000 && s_ml && !microlink_control_busy(s_ml)) break;
  }
  TLOGW("control refresh: no new map after %lu ms", (unsigned long)waited);
  return false;
}

// Bring up the tunnel; with one-shot control, a peer that does not answer
// may have moved (new key, relay or endpoints) since the map was fetched:
// fetch a fresh one once per WiFi session and try again.
static void bringUpTunnelOrRefresh(uint32_t ip, uint32_t timeout_ms) {
  if (bringUpTunnel(ip, timeout_ms)) return;
  if (!microlink_control_oneshot() || s_tunnel_refreshed.exchange(ip) == ip) return;
  TLOGW("tunnel to %u.%u.%u.%u: refreshing the map and retrying", IP_ARGS(ip));
  if (refreshControl(timeout_ms, "no WireGuard handshake")) bringUpTunnel(ip, timeout_ms);
}

extern "C" bool tailnet_ensure_peer(uint32_t ip, uint32_t timeout_ms) {
  s_last_target.store(ip);
  char what[24];
  snprintf(what, sizeof(what), "%u.%u.%u.%u", IP_ARGS(ip));
  tailnet_start_on_demand(what);
  char label[40];
  snprintf(label, sizeof(label), "connect %s", what);
  if (!tailnet_wait_connected_for(label, timeout_ms)) {
    TLOGW("connect %u.%u.%u.%u: tailnet not connected", IP_ARGS(ip));
    return false;
  }
#if CONFIG_ML_LITE
  // Let a running control session finish first: its memory comes back when
  // it closes, and the caller's TLS handshake is next.
  for (uint32_t waited = 0; s_ml && microlink_control_busy(s_ml) && waited < timeout_ms; waited += 50) {
    vTaskDelay(pdMS_TO_TICKS(50));
  }
#endif
  // A fresh map may still be filling the table just after CONNECTED.
  for (int i = 0; i < 30 && !peerKnown(ip); i++) vTaskDelay(pdMS_TO_TICKS(100));
  if (peerKnown(ip)) {
    rememberServer(ip);
    bringUpTunnelOrRefresh(ip, timeout_ms);
    return true;
  }
  if (ip == s_pin.load() || !markTried(ip)) {
    TLOGW("connect %u.%u.%u.%u: not a peer of this reader (check ACLs)", IP_ARGS(ip));
    return false;
  }
  TLOGW("connect %u.%u.%u.%u: not in peer table (%d peers); pinning", IP_ARGS(ip),
           s_ml ? microlink_get_peer_count(s_ml) : 0);
  storePin(ip);  // remembered across reboots: pinned from the next start on
  if (microlink_control_oneshot()) {
    // One-shot control: a fresh map with the pin, no restart.
    if (!refreshControl(timeout_ms, "peer not in the table")) return false;
  } else {
    post(REQ_REPIN);
    // Wait for the restart to begin, then for it to finish.
    for (int i = 0; i < 50 && tailnet_status() == TAILNET_CONNECTED; i++) vTaskDelay(pdMS_TO_TICKS(100));
    if (!tailnet_wait_connected(timeout_ms)) return false;
  }
  for (int i = 0; i < 50 && !peerKnown(ip); i++) vTaskDelay(pdMS_TO_TICKS(100));
  const bool known = peerKnown(ip);
  if (known) {
    TLOGW("connect %u.%u.%u.%u: peer added after pinning", IP_ARGS(ip));
    rememberServer(ip);
    bringUpTunnelOrRefresh(ip, timeout_ms);
  } else {
    TLOGW("connect %u.%u.%u.%u: still not a peer after pinning (check ACLs)", IP_ARGS(ip));
  }
  return known;
}

extern "C" void tailnet_get_diag(tailnet_diag_t* out) {
  if (!out) return;
  *out = {};
  out->peer_capacity = CONFIG_ML_MAX_PEERS;
  out->pin_ip = s_pin.load();
  out->target_ip = s_last_target.load();
  if (!out->target_ip) out->target_ip = out->pin_ip;
  if (!s_ml || tailnet_status() != TAILNET_CONNECTED) return;
  out->peer_count = microlink_get_peer_count(s_ml);
  microlink_peer_info_t info;
  for (int i = 0; i < out->peer_count; i++) {
    if (microlink_get_peer_info(s_ml, i, &info) == ESP_OK && info.vpn_ip == out->target_ip && out->target_ip) {
      out->target_known = true;
      out->target_online = info.online;
      out->target_direct = info.direct_path;
    }
  }
  tailnet_ml_relay_codes(s_ml, out->target_ip, out->home_relay, out->target_relay, sizeof(out->home_relay));
}

extern "C" tailnet_relay_mode_t tailnet_relay_mode(void) {
  loadRelay();
  return (tailnet_relay_mode_t)s_relay_mode.load();
}

extern "C" uint16_t tailnet_relay_fixed_region(void) {
  loadRelay();
  return (uint16_t)s_relay_fixed.load();
}

extern "C" void tailnet_set_relay(tailnet_relay_mode_t mode, uint16_t region) {
  loadRelay();
  if (mode == TAILNET_RELAY_FIXED) {
    if (!region) return;
    nvsSetU32(NVS_KEY_RELAY_FIXED, region);
    s_relay_fixed.store(region);
  }
  nvsSetU32(NVS_KEY_RELAY_MODE, mode);
  s_relay_mode.store(mode);
  s_generation.fetch_add(1);
  post(REQ_RELAY);
}

extern "C" int tailnet_relay_regions(uint16_t* ids, char (*codes)[8], int max) {
  return s_ml ? tailnet_ml_regions(s_ml, ids, codes, max) : 0;
}

extern "C" void tailnet_relay_code(uint16_t id, char* out, size_t len) {
  if (!out || !len) return;
  out[0] = 0;
  if (!id) return;
  uint16_t ids[32];
  char codes[32][8];
  const int n = tailnet_relay_regions(ids, codes, 32);
  for (int i = 0; i < n; i++) {
    if (ids[i] == id) {
      snprintf(out, len, "%s", codes[i]);
      return;
    }
  }
  snprintf(out, len, "region %u", (unsigned)id);
}

extern "C" bool tailnet_lite(void) {
#if CONFIG_ML_LITE
  return true;
#else
  return false;
#endif
}

extern "C" void tailnet_permit_on_demand(void) {
#if CONFIG_ML_LITE
  s_on_demand = true;
#endif
}

extern "C" bool tailnet_start_on_demand(const char* why) {
#if CONFIG_ML_LITE
  if (!s_on_demand.load() || s_permit.load() || !loadEnabled() || !tailnet_joined()) return false;
  TLOGI("%s needs Tailscale: starting it for this boot", why);
  tailnet_permit_start(false);
  return true;
#else
  (void)why;
  return false;
#endif
}

extern "C" void tailnet_permit_start(bool register_only) {
#if CONFIG_ML_LITE
  s_register_only = register_only;
  s_permit = true;
  post(REQ_RECHECK);
#else
  (void)register_only;
#endif
}

// Which path carries traffic to the server now, with its round trip: one log
// line for a transfer's summary (e.g. "direct 192.168.1.20:41641, rtt 4 ms").
extern "C" void tailnet_log_path(const char* what) {
  char path[64];
  if (!s_ml) return;
  tailnet_ml_path(s_ml, serverIp(), path, sizeof(path));
  TLOGI("%s: path %s", what ? what : "transfer", path);
}

extern "C" void tailnet_probe_permit_unparked(void) {
#if CONFIG_ML_LITE
  s_unparked = true;
  tailnet_permit_start(false);
#endif
}

extern "C" void tailnet_catalog_connect(bool allow_control) {
#if CONFIG_ML_LITE
  s_catalog_result = TAILNET_CATALOG_PENDING;
  s_catalog_mode = allow_control ? TAILNET_CATALOG_ALLOW_CONTROL : TAILNET_CATALOG_CACHED_ONLY;
  tailnet_permit_start(false);
#else
  (void)allow_control;
#endif
}

extern "C" int tailnet_catalog_result(void) {
#if CONFIG_ML_LITE
  return s_catalog_result.load();
#else
  return TAILNET_CATALOG_FAILED;
#endif
}

extern "C" void tailnet_probe_heap(const char* step) {
  TLOGI("probe %s: heap free %u, largest %u, lowest so far %u (internal free %u)", step,
                   (unsigned)esp_get_free_heap_size(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                   (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

// A request's heap window: the lowest point of each phase, via ESP-IDF's
// local minimum monitor (restarted at every phase boundary).
static bool s_probe_window = false;

static size_t probeLocalLow() { return heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT); }

static void probeRestartMonitor() {
  if (s_probe_window) heap_caps_monitor_local_minimum_free_size_stop();
  s_probe_window = heap_caps_monitor_local_minimum_free_size_start() == ESP_OK;
}

extern "C" void tailnet_probe_window_begin(const char* what) {
  TLOGI("probe %s: before: heap free %u, largest %u", what, (unsigned)esp_get_free_heap_size(),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  probeRestartMonitor();
}

extern "C" void tailnet_probe_window_mark(const char* what, const char* phase) {
  const size_t free_now = esp_get_free_heap_size();
  TLOGI("probe %s: %s: lowest %u (drop %d from free now %u), largest now %u", what, phase, (unsigned)probeLocalLow(),
        (int)free_now - (int)probeLocalLow(), (unsigned)free_now,
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  probeRestartMonitor();
}

extern "C" void tailnet_probe_window_end(const char* what, const char* phase) {
  tailnet_probe_window_mark(what, phase);
  if (s_probe_window) heap_caps_monitor_local_minimum_free_size_stop();
  s_probe_window = false;
  TLOGI("probe %s: done; lowest since boot %u", what, (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
  static bool s_reported = false;  // TailnetStacks.c: after the first HTTPS request
  if (!s_reported) {
    s_reported = true;
    tailnet_stack_report("first HTTPS request done");
  }
}

extern "C" tailnet_status_t tailnet_login_session(uint32_t timeout_ms) {
  const TickType_t start = xTaskGetTickCount();
  for (;;) {
    const tailnet_status_t st = tailnet_status();
    char url[TAILNET_LOGIN_URL_MAX];
    if (st == TAILNET_CONNECTED || st == TAILNET_FAILED || (st == TAILNET_NEEDS_LOGIN && tailnet_login_url(url, sizeof(url)))) {
      return st;
    }
    if ((xTaskGetTickCount() - start) >= pdMS_TO_TICKS(timeout_ms)) {
      TLOGW("Login session: no answer from Tailscale after %lu ms", (unsigned long)timeout_ms);
      return st;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

extern "C" bool tailnet_saved_login_url(char* buf, size_t len) {
  if (!buf || len == 0) return false;
  buf[0] = '\0';
#if CONFIG_ML_LITE
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
    size_t n = len;
    if (nvs_get_str(h, NVS_KEY_LOGIN_URL, buf, &n) != ESP_OK) buf[0] = '\0';
    nvs_close(h);
  }
#endif
  return buf[0] != '\0';
}

namespace {

void logHeap(const char* what, TickType_t start) {
  TLOGI("%s after %lu ms: heap free %u, largest block %u", what, (unsigned long)elapsedMs(start),
        (unsigned)esp_get_free_heap_size(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}
}  // namespace

extern "C" bool tailnet_wait_ready(uint32_t timeout_ms) {
#if CONFIG_ML_LITE
  // Only the parked network boots run Tailscale, and the login boot needs no
  // traffic, only its register-only session.
  if (!s_permit.load() || s_register_only.load() || !loadEnabled() || !tailnet_joined()) return true;
  const TickType_t start = xTaskGetTickCount();
  auto left = [&]() -> uint32_t {
    const uint32_t e = elapsedMs(start);
    return e < timeout_ms ? timeout_ms - e : 0;
  };
  TLOGI("Waiting for Tailscale before the first request (up to %lu ms)", (unsigned long)timeout_ms);
  if (!tailnet_wait_connected(timeout_ms)) {
    TLOGW("Tailscale not ready (status %d)", (int)tailnet_status());
    logHeap("Tailscale wait over", start);
    return false;
  }
  // The one-shot control session closes once its map is in: its task stack
  // and Noise/H2 buffers come back then, and the request's heap check needs them.
  while (s_ml && microlink_control_busy(s_ml) && left() > 0) vTaskDelay(pdMS_TO_TICKS(50));
  if (s_ml && microlink_control_busy(s_ml)) TLOGW("Control session still running");
  const uint32_t ip = serverIp();
  bool up = false;
  if (ip && s_ml) {
    for (int i = 0; i < 30 && !peerKnown(ip) && left() > 0; i++) vTaskDelay(pdMS_TO_TICKS(100));
    if (peerKnown(ip)) {
      up = left() > 0 && bringUpTunnel(ip, left());
    } else {
      TLOGW("Target peer %u.%u.%u.%u not in the peer table yet", IP_ARGS(ip));
    }
  }
  TLOGI("Tailscale ready: server %u.%u.%u.%u, tunnel %s", IP_ARGS(ip), up ? "up" : "not up yet");
  logHeap("Tailscale wait over", start);
  return true;
#else
  (void)timeout_ms;
  return true;
#endif
}

extern "C" void tailnet_stop_before_wifi_off(uint32_t timeout_ms) {
  // MicroLink's stop takes ~1 s (the relay task leaves its blocking read). Turned off
  // without waiting, WiFi was deinitialised under its still-running tasks, and the
  // caller went on (the sleep sync: redraw and deep sleep) while they tore down.
  // The worker, MicroLink's own tasks and the event tasks must never wait here.
  if (!s_ml_live.load() || !mayWaitForTailscale()) return;
#if TAILNET_LAZY_WORKER
  s_link_up.store(false);
#endif
  post(REQ_WIFI_DOWN);
  const TickType_t start = xTaskGetTickCount();
  while (s_ml_live.load()) {
    if (elapsedMs(start) >= timeout_ms) {
      TLOGW("MicroLink still stopping after %lu ms; turning WiFi off anyway", (unsigned long)timeout_ms);
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  TLOGI("MicroLink stopped before WiFi off (%lu ms)", (unsigned long)elapsedMs(start));
}

extern "C" void tailnet_wait_idle(uint32_t timeout_ms) {
  // Before WiFi goes off: a control session cut mid-exchange leaves the coord
  // task retrying against a dead socket (errno 113) until it gives up.
  const TickType_t start = xTaskGetTickCount();
  while (s_ml && microlink_control_busy(s_ml)) {
    if (elapsedMs(start) >= timeout_ms) {
      TLOGW("Control session still running after %lu ms; turning WiFi off anyway", (unsigned long)timeout_ms);
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

extern "C" bool tailnet_service_stop_and_wait(uint32_t timeout_ms) {
  if (!mayWaitForTailscale()) return false;
  // Keep F10's owner token until teardown completes, but disable transport start.
  s_link_up.store(false);
  post(REQ_WIFI_DOWN);
  const TickType_t start = xTaskGetTickCount();
  for (;;) {
    bool workerLive = false;
#if TAILNET_LAZY_WORKER
    if (s_worker_lock) {
      xSemaphoreTake(s_worker_lock, portMAX_DELAY);
      workerLive = s_worker != nullptr;
      xSemaphoreGive(s_worker_lock);
    }
#endif
    if (!workerLive && !s_ml_live.load()) return true;
    if (elapsedMs(start) >= timeout_ms) return false;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
