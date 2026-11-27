// Tailnet names and addresses for the whole firmware, without MagicDNS.
//
// Every resolver path in CrossInk ends in lwip_getaddrinfo(), and every TCP
// client in lwip_connect(): Arduino's NetworkClient and HTTPClient, the wolfSSL
// SecureHttpClient, and esp-tls (esp_http_client: OTA and downloads).
// tailnet_build.py links with --wrap for both, so callers in the prebuilt
// libraries land here.
//
// Names: a peer MicroLink knows ("peer", "peer.<tail>.ts.net") is
// answered by passing its 100.x address to the real lwip_getaddrinfo as a
// numeric host, which builds (and lwip_freeaddrinfo later frees) the result the
// usual way. Anything else goes to DNS untouched.
//
// Timing: CrossInk starts network work (a library sync) the moment WiFi has
// an address, while the tailnet needs a few seconds more. Lookups, and connects
// to tailnet addresses, wait for it while it is starting (up to WAIT_MS).
//
// Large tailnets: MicroLink tracks a limited number of peers. A connect to a
// tailnet address it is not tracking pins that address as its priority peer
// and restarts it, so the server is added (see tailnet_ensure_peer).
#include <lwip/netdb.h>
#include <lwip/sockets.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>

#include "Tailnet.h"

namespace {
constexpr uint32_t WAIT_MS = 20000;

// Names worth waiting for: single-label names ("peer") and MagicDNS names
// ("peer.<tail>.ts.net"). Public names (api.github.com, and MicroLink's
// own controlplane.tailscale.com and DERP lookups) never wait.
bool mayBeTailnetName(const char* name) {
  const size_t n = strlen(name);
  const size_t m = n && name[n - 1] == '.' ? n - 1 : n;  // ignore a trailing dot
  if (!memchr(name, '.', m)) return true;
  static constexpr char kSuffix[] = ".ts.net";
  constexpr size_t k = sizeof(kSuffix) - 1;
  return m > k && strncasecmp(name + m - k, kSuffix, k) == 0;
}

// Tailscale's IPv4 range, 100.64.0.0/10 (host byte order).
bool isTailnetAddr(uint32_t ip) { return (ip & 0xFFC00000u) == 0x64400000u; }
}  // namespace

extern "C" int __real_lwip_getaddrinfo(const char* nodename, const char* servname, const struct addrinfo* hints,
                                       struct addrinfo** res);
extern "C" int __real_lwip_connect(int s, const struct sockaddr* name, socklen_t namelen);
extern "C" bool tailnet_lwip_started(void);  // TailnetLwipLock.c

extern "C" int __wrap_lwip_getaddrinfo(const char* nodename, const char* servname, const struct addrinfo* hints,
                                       struct addrinfo** res) {
  // No TCP/IP stack yet (Wi-Fi never started this boot): the real lookup would assert on the core
  // lock (TailnetLwipLock.c). It fails like a lookup with Wi-Fi down.
  if (!tailnet_lwip_started()) {
    tailnet_log_emit("W (%lu) tailnet: lookup of %.48s refused: the network is not started\n",
                     (unsigned long)esp_log_timestamp(), nodename ? nodename : "(null)");
    return EAI_FAIL;
  }
  const bool v4_ok = !hints || hints->ai_family == AF_UNSPEC || hints->ai_family == AF_INET;
  const bool numeric_only = hints && (hints->ai_flags & AI_NUMERICHOST);
  ip4_addr_t probe;
  if (nodename && v4_ok && !numeric_only && !ip4addr_aton(nodename, &probe)) {
    uint32_t ip = tailnet_resolve(nodename);
    if (!ip && mayBeTailnetName(nodename)) {
      tailnet_start_on_demand(nodename);  // lite builds: a boot that starts Tailscale only when needed
      char what[64];
      snprintf(what, sizeof(what), "resolve %.48s", nodename);
      if (tailnet_wait_connected_for(what, WAIT_MS)) ip = tailnet_resolve(nodename);
    }
    if (ip) {
      char lit[16];
      snprintf(lit, sizeof(lit), "%u.%u.%u.%u", (unsigned)(ip >> 24) & 0xff, (unsigned)(ip >> 16) & 0xff,
               (unsigned)(ip >> 8) & 0xff, (unsigned)ip & 0xff);
      return __real_lwip_getaddrinfo(lit, servname, hints, res);
    }
  }
  return __real_lwip_getaddrinfo(nodename, servname, hints, res);
}

namespace {
// tailnet_connect_count(): the task counted, and its connects so far.
std::atomic<TaskHandle_t> s_counted_task{nullptr};
std::atomic<uint32_t> s_counted_connects{0};
}  // namespace

extern "C" uint32_t tailnet_connect_count(void) {
  TaskHandle_t expected = nullptr;
  s_counted_task.compare_exchange_strong(expected, xTaskGetCurrentTaskHandle());
  return s_counted_connects.load();
}

extern "C" int __wrap_lwip_connect(int s, const struct sockaddr* name, socklen_t namelen) {
  if (s_counted_task.load() == xTaskGetCurrentTaskHandle()) s_counted_connects.fetch_add(1);
  if (name && name->sa_family == AF_INET && namelen >= sizeof(struct sockaddr_in) &&
      reinterpret_cast<const struct sockaddr_in*>(name)->sin_addr.s_addr == 0) {
    // 0.0.0.0: the name did not resolve (e.g. the network was turned off).
    // lwIP would wait out the whole connect timeout (~14 s) for nothing.
    tailnet_log_emit("W (%lu) tailnet: connect 0.0.0.0 refused at once (name not resolved)\n",
                     (unsigned long)esp_log_timestamp());
    errno = EHOSTUNREACH;
    return -1;
  }
  if (name && name->sa_family == AF_INET && namelen >= sizeof(struct sockaddr_in) && tailnet_enabled()) {
    const auto* sin = reinterpret_cast<const struct sockaddr_in*>(name);
    const uint32_t ip = lwip_ntohl(sin->sin_addr.s_addr);
    const unsigned port = lwip_ntohs(sin->sin_port);
    // Every outgoing connection is logged for Settings > Tailscale >
    // Diagnostics: it shows whether a server name resolved to its tailnet
    // address at all.
    tailnet_log_emit("W (%lu) tailnet: connect %u.%u.%u.%u:%u%s\n",
                  (unsigned long)esp_log_timestamp(), (unsigned)(ip >> 24) & 0xff, (unsigned)(ip >> 16) & 0xff,
                  (unsigned)(ip >> 8) & 0xff, (unsigned)ip & 0xff, port, isTailnetAddr(ip) ? " (tailnet)" : "");
    if (isTailnetAddr(ip) && !tailnet_ensure_peer(ip, WAIT_MS)) {
      // Connected but not a peer (MicroLink would drop the packets), or
      // Tailscale is not up in this boot (the packets would go out over WiFi
      // to nowhere): the caller would wait out its whole timeout. Fail now.
      errno = EHOSTUNREACH;
      return -1;
    }
  }
  return __real_lwip_connect(s, name, namelen);
}
