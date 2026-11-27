// SPDX-License-Identifier: GPL-3.0-or-later
#include <cassert>
#include "TailnetNetPhase.h"
static int changes = 0;
extern "C" void tailnet_service_changed(void) { ++changes; }
int main() {
  assert(!tailnet_service_permitted());
  tailnet_set_reading(true);
  assert(changes == 0);
  assert(!tailnet_service_acquire(TAILNET_JOB_PAIRING, true).epoch);
  tailnet_set_reading(false);
  auto first = tailnet_service_acquire(TAILNET_JOB_PAIRING, true);
  assert(first.epoch && tailnet_service_permitted() && tailnet_display_park());
  tailnet_set_reading(false);
  assert(changes == 2 && tailnet_service_permitted());
  assert(!tailnet_service_acquire(TAILNET_JOB_TRANSFER, true).epoch);
  tailnet_set_reading(true);
  assert(!tailnet_service_permitted() && !tailnet_display_park());
  tailnet_set_reading(false);
  assert(tailnet_service_job() == TAILNET_JOB_NONE);
  auto second = tailnet_service_acquire(TAILNET_JOB_TRANSFER, false);
  tailnet_service_release(first);
  assert(changes == 5);
  assert(second.epoch != first.epoch && tailnet_service_permitted() && !tailnet_display_park());
  tailnet_service_release(second);
  tailnet_service_release(second);
  assert(!tailnet_service_permitted());
  assert(!tailnet_service_acquire(static_cast<tailnet_service_job_t>(0), true).epoch);
}
