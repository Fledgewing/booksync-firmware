#pragma once
#include <stdbool.h>
typedef enum { LWIP_CORE_LOCK_QUERY_HOLDER, LWIP_CORE_LOCK_MARK_HOLDER, LWIP_CORE_LOCK_UNMARK_HOLDER } sys_thread_core_lock_t;
bool sys_thread_tcpip(sys_thread_core_lock_t type);
// lwIP's core lock: NULL until tcpip_init() (esp_netif_init) creates it.
typedef void* sys_mutex_t;
extern sys_mutex_t lock_tcpip_core;
void stub_lock(void);
void stub_unlock(void);
#define LOCK_TCPIP_CORE() stub_lock()
#define UNLOCK_TCPIP_CORE() stub_unlock()
