// Host test for the lwIP wrappers (tailnet/shim/TailnetLwipLock.c): the real cache clear always
// runs with the lwIP core lock held, taken once by the wrapper when the caller does not hold it,
// and never taken twice (the core lock is not recursive); before the TCP/IP stack exists (the core
// lock is still NULL: Wi-Fi never started this boot) no lock is taken and no socket is made.
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>

#include "lwip/tcpip.h"

void __wrap_dns_clear_cache(void);
int __wrap_lwip_socket(int domain, int type, int protocol);
bool tailnet_lwip_started(void);

static int stackLock;
sys_mutex_t lock_tcpip_core = &stackLock;

static bool holder = false;
static int locks = 0, unlocks = 0, clears = 0, clearsUnlocked = 0, doubleLocks = 0, sockets = 0, nullLocks = 0;

bool sys_thread_tcpip(sys_thread_core_lock_t type) { return type == LWIP_CORE_LOCK_QUERY_HOLDER && holder; }
void stub_lock(void) {
  // The device's sys_mutex_lock(&lock_tcpip_core): xQueueSemaphoreTake(NULL) asserts (.194 panic).
  if (lock_tcpip_core == NULL) nullLocks++;
  if (holder) doubleLocks++;
  holder = true;
  locks++;
}
void stub_unlock(void) {
  holder = false;
  unlocks++;
}
void __real_dns_clear_cache(void) {
  clears++;
  if (!holder) clearsUnlocked++;
}
int __real_lwip_socket(int domain, int type, int protocol) {
  (void)domain, (void)type, (void)protocol;
  return 54 + sockets++;
}

static int failed = 0, passed = 0;
#define CHECK(c) ((c) ? (void)passed++ : (void)(failed++, printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c)))

int main(void) {
  // An app task (hostByName): the wrapper locks around the clear and releases.
  CHECK(tailnet_lwip_started());
  __wrap_dns_clear_cache();
  CHECK(clears == 1 && clearsUnlocked == 0);
  CHECK(locks == 1 && unlocks == 1 && !holder);
  // tcpip_thread or esp_netif_tcpip_exec (TrustedTime, Tailnet.cpp): already locked, not again.
  holder = true;
  __wrap_dns_clear_cache();
  CHECK(clears == 2 && clearsUnlocked == 0);
  CHECK(locks == 1 && unlocks == 1 && holder && doubleLocks == 0);
  holder = false;
  CHECK(__wrap_lwip_socket(2, 1, 0) == 54 && sockets == 1);

  // The cache-first catalog (X4 Pro, .194): Wi-Fi never started, so tcpip_init never ran.
  lock_tcpip_core = NULL;
  CHECK(!tailnet_lwip_started());
  __wrap_dns_clear_cache();  // nothing to race with: cleared without the (missing) lock
  CHECK(clears == 3 && locks == 1 && nullLocks == 0);
  errno = 0;
  CHECK(__wrap_lwip_socket(2, 1, 0) == -1 && errno == ENETDOWN && sockets == 1);
  printf("lwip-lock: %d passed, %d failed\n", passed, failed);
  return failed ? 1 : 0;
}
