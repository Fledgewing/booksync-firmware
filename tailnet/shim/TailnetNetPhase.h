// SPDX-License-Identifier: GPL-3.0-or-later
// Worker-only transport admission. F11 supplies display ownership; no activity IDs,
// navigation, display allocation or restart is permitted in this module.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
  TAILNET_JOB_NONE = 0,
  TAILNET_JOB_PAIRING = 1,
  TAILNET_JOB_TRANSFER,
  TAILNET_JOB_CATALOG,
  TAILNET_JOB_UPDATE
} tailnet_service_job_t;
typedef struct { uint32_t epoch; } tailnet_service_lease_t;
// Fail closed until runtime explicitly reports that reading is inactive.
void tailnet_set_reading(bool reading);
// One owner; display_released is the runtime's proof of C3 framebuffer ownership.
tailnet_service_lease_t tailnet_service_acquire(tailnet_service_job_t job, bool display_released);
void tailnet_service_release(tailnet_service_lease_t lease);
bool tailnet_service_permitted(void);
bool tailnet_display_park(void);
void tailnet_display_unpark(void);
tailnet_service_job_t tailnet_service_job(void);
// Cached-start decision retained from the transport shim; never restarts.
enum { TAILNET_CATALOG_NONE = 0, TAILNET_CATALOG_ALLOW_CONTROL = 1, TAILNET_CATALOG_CACHED_ONLY = 2 };
enum { TAILNET_CATALOG_PENDING = 0, TAILNET_CATALOG_CACHED = 1, TAILNET_CATALOG_CONTROL = 2,
       TAILNET_CATALOG_REFUSED = 3, TAILNET_CATALOG_FAILED = 4 };
void tailnet_catalog_connect(bool allow_control);
int tailnet_catalog_result(void);
#ifdef __cplusplus
}
#endif
