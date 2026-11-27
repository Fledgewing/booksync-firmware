// Log of MicroLink and shim messages for the Tailscale screen, kept across restarts.
//
// The prebuilt framework has CONFIG_LOG_MAXIMUM_LEVEL=ERROR, and
// esp_log_level_set() cannot raise a tag above it: INFO and WARN lines from
// MicroLink and the shim are dropped inside esp_log before any vprintf hook
// sees them. So the link wraps esp_log (MicroLink's ESP_LOGx;
// tailnet_build.py), and the shim logs through tailnet_log_emit: lines from our tags are
// kept here and printed to the console directly.
#include <esp_attr.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "Tailnet.h"

namespace {

// CrossInk restarts the chip around every network task (SilentRestart.h: sync,
// catalog, downloads), so a log in ordinary RAM is gone before anyone opens
// the Diagnostics page. RTC_NOINIT survives software restarts (as CrossInk's
// silentReboot flags do); a power-on leaves garbage, caught by the magic.
constexpr int LINES = 16;
constexpr int WIDTH = 80;  // RTC memory is small: ~1.7 KB free on the S3
constexpr uint32_t MAGIC = 0x544c4f47;  // "TLOG"

struct Ring {
  uint32_t magic;
  int head;       // next slot to write
  int count;      // lines held
  uint32_t seq;   // lines ever stored
  char lines[LINES][WIDTH];
};
RTC_NOINIT_ATTR Ring s_ring;
bool s_checked = false;  // this boot
portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

const char* const TAGS[] = {"tailnet",  "microlink", "ml_coord", "ml_coord_tls", "ml_derp",    "ml_filter", "ml_h2",
                            "ml_net_io", "ml_noise",  "ml_peer_nvs", "ml_stun",  "ml_tcp",     "ml_udp",    "ml_wg_mgr",
                            "ml_zc",    "wg"};

bool ours(const char* tag) {
  if (!tag) return false;
  for (const char* t : TAGS) {
    if (strcmp(tag, t) == 0) return true;
  }
  return false;
}

void push(const char* text) {
  const size_t n = strnlen(text, WIDTH - 1);
  memcpy(s_ring.lines[s_ring.head], text, n);
  s_ring.lines[s_ring.head][n] = '\0';
  s_ring.head = (s_ring.head + 1) % LINES;
  if (s_ring.count < LINES) s_ring.count++;
  s_ring.seq++;
}

// Under s_lock. First touch this boot: keep a valid ring and mark the restart,
// or start over.
void checkRing() {
  if (s_checked) return;
  s_checked = true;
  if (s_ring.magic != MAGIC || s_ring.head < 0 || s_ring.head >= LINES || s_ring.count < 0 ||
      s_ring.count > LINES) {
    memset(&s_ring, 0, sizeof(s_ring));
    s_ring.magic = MAGIC;
  } else if (s_ring.count) {
    push("--- restart ---");
  }
}

void store(const char* fmt, va_list args) {
  char line[WIDTH + 24];
  vsnprintf(line, sizeof(line), fmt, args);

  // Drop ANSI colour codes and the trailing newline; skip blank lines.
  char clean[WIDTH];
  size_t j = 0;
  for (size_t i = 0; line[i] && j < sizeof(clean) - 1; i++) {
    if (line[i] == '\033') {
      while (line[i] && line[i] != 'm') i++;
      if (!line[i]) break;
      continue;
    }
    if (line[i] == '\n' || line[i] == '\r') continue;
    clean[j++] = line[i];
  }
  clean[j] = '\0';
  if (!j) return;
  portENTER_CRITICAL(&s_lock);
  checkRing();
  push(clean);
  portEXIT_CRITICAL(&s_lock);
}

}  // namespace

extern "C" {
// ESP_LOGx (log v1) expands to esp_log(); the format already carries the
// level letter, timestamp and tag.
void __wrap_esp_log(esp_log_config_t config, const char* tag, const char* format, ...) {
  va_list args;
  va_start(args, format);
  if (!config.opts.constrained_env && ours(tag)) {
    // Ours: keep it, and print it ourselves (esp_log_va would drop it).
    va_list copy;
    va_copy(copy, args);
    store(format, copy);
    va_end(copy);
    vprintf(format, args);
  } else {
    esp_log_va(config, tag, format, args);
  }
  va_end(args);
}

}

// The shim's own lines (TLOG, connect lines): kept and printed directly.
// Not a wrap of esp_log_write: the C3 build's Arduino core already wraps it
// (esp32-hal-log-wrapper.c).
extern "C" void tailnet_log_emit(const char* format, ...) {
  va_list args;
  va_start(args, format);
  va_list copy;
  va_copy(copy, args);
  store(format, copy);
  va_end(copy);
  vprintf(format, args);
  va_end(args);
}

extern "C" void tailnet_log_install(void) {}

extern "C" bool tailnet_log_line(int newest_index, char* buf, size_t len) {
  if (!buf || !len) return false;
  bool ok = false;
  portENTER_CRITICAL(&s_lock);
  checkRing();
  if (newest_index >= 0 && newest_index < s_ring.count) {
    const int slot = (s_ring.head - 1 - newest_index + LINES) % LINES;
    strncpy(buf, s_ring.lines[slot], len - 1);
    buf[len - 1] = '\0';
    ok = true;
  }
  portEXIT_CRITICAL(&s_lock);
  return ok;
}

extern "C" uint32_t tailnet_log_seq(void) {
  portENTER_CRITICAL(&s_lock);
  checkRing();
  const uint32_t v = s_ring.seq;
  portEXIT_CRITICAL(&s_lock);
  return v;
}
