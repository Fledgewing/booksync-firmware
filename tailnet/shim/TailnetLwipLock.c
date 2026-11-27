// lwIP calls made outside tcpip_thread without the core lock (CONFIG_LWIP_TCPIP_CORE_LOCKING with
// CONFIG_LWIP_CHECK_THREAD_SAFETY: the check asserts and the reader panics), and lwIP calls made
// before the TCP/IP stack exists.
//
// dns_clear_cache(): Arduino's NetworkManager::hostByName() (libraries/Network, compiled from
// source) calls it from the caller's task whenever the device's IPv4/IPv6 state changed since
// the last lookup, i.e. on the first lookup after WiFi joins. With another task's lookup in
// flight (MicroLink's STUN servers, resolving in parallel) the cache clear finishes that lookup
// (dns_call_found) and frees its random-port UDP PCB: udp_remove() off tcpip_thread.
// Seen on an X4 Pro, xteink-v2.0.0.141, catalog boot after "Restarting into the catalog":
//   W ml_stun: DNS resolve failed for derp9.tailscale.com
//   assert failed: udp_remove udp.c:1199 (Required to lock TCPIP core functionality!)
//   backtrace: udp_remove <- dns_call_found <- dns_clear_cache <- NetworkManager::hostByName
//              <- freeink::hostResolves <- SecureClient::connect <- ... fetchBookDetail
// The link wraps dns_clear_cache (tailnet_build.py); this wrapper takes the core lock unless the
// caller already holds it (tcpip_thread, or CrossInk's TrustedTime via esp_netif_tcpip_exec).
//
// Before the stack exists: tcpip_init() (esp_netif_init(), the first time Wi-Fi starts) creates
// the core lock. Until then every lwIP API call that takes it (a name lookup, a new socket) asserts
// on a NULL mutex. Seen on an X4 Pro, xteink-v2.0.0.194, the cache-first catalog
// opened with Wi-Fi never started, then a list's next page:
//   assert failed: xQueueSemaphoreTake queue.c:1709 (( pxQueue ))
//   backtrace: xQueueSemaphoreTake(NULL) <- sys_mutex_lock(&lock_tcpip_core)
//              <- tcpip_send_msg_wait_sem <- netconn_gethostbyname <- lwip_getaddrinfo
//              <- NetworkManager::hostByName <- SecureClient::connect <- ... fetchSectionEntries(page 2)
// tailnet_lwip_started() says whether the stack is up; the lookup (TailnetResolver.cpp) and
// lwip_socket (wrapped here) fail at once without it, as they would with Wi-Fi down.
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>

#include "lwip/opt.h"
#include "lwip/dns.h"
#include "lwip/tcpip.h"

void __real_dns_clear_cache(void);
int __real_lwip_socket(int domain, int type, int protocol);

bool tailnet_lwip_started(void) {
#if LWIP_TCPIP_CORE_LOCKING
  return lock_tcpip_core != NULL;
#else
  return true;
#endif
}

void __wrap_dns_clear_cache(void) {
#if LWIP_TCPIP_CORE_LOCKING
  // No stack: no tcpip_thread and no other lookup to race with, and no lock to take.
  if (!tailnet_lwip_started() || sys_thread_tcpip(LWIP_CORE_LOCK_QUERY_HOLDER)) {
    __real_dns_clear_cache();
    return;
  }
  LOCK_TCPIP_CORE();
  __real_dns_clear_cache();
  UNLOCK_TCPIP_CORE();
#else
  __real_dns_clear_cache();
#endif
}

int __wrap_lwip_socket(int domain, int type, int protocol) {
  if (!tailnet_lwip_started()) {
    errno = ENETDOWN;
    return -1;
  }
  return __real_lwip_socket(domain, type, protocol);
}
