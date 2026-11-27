// SPDX-License-Identifier: GPL-3.0-or-later
#include "TailnetNetPhase.h"

#include <atomic>

extern "C" __attribute__((weak)) void tailnet_service_changed(void) {}

namespace {
// Packed state gives worker admission one coherent snapshot. No heap or renderer access.
constexpr uint32_t kReading = 1u;
constexpr uint32_t kReleased = 2u;
constexpr uint32_t kJobMask = 28u;
constexpr uint32_t kReady = 32u;
constexpr uint32_t kEpochStep = 64u;
std::atomic<uint32_t> state{kReading};
}  // namespace
extern "C" void tailnet_set_reading(bool reading) {
  uint32_t previous = state.load();
  uint32_t next;
  do {
    if (((previous & kReading) != 0) == reading) return;
    // Entering reading revokes the lease; stale releases cannot revoke a new owner.
    next = reading ? ((previous & ~(kEpochStep - 1)) + kEpochStep) | kReading : previous & ~kReading;
  } while (!state.compare_exchange_weak(previous, next));
  tailnet_service_changed();
}
static tailnet_service_lease_t acquire(tailnet_service_job_t job, bool released, bool ready) {
  if (job < TAILNET_JOB_PAIRING || job > TAILNET_JOB_UPDATE) return {0};
  uint32_t previous = state.load();
  uint32_t next;
  do {
    if (previous & (kReading | kJobMask)) return {0};
    next = ((previous & ~(kEpochStep - 1)) + kEpochStep) | (static_cast<uint32_t>(job) << 2) |
           (released ? kReleased : 0) | (ready ? kReady : 0);
    if ((next & ~(kEpochStep - 1)) == 0) next += kEpochStep;
  } while (!state.compare_exchange_weak(previous, next));
  tailnet_service_changed();
  return {next & ~(kEpochStep - 1)};
}
extern "C" void tailnet_service_release(tailnet_service_lease_t lease) {
  uint32_t previous = state.load();
  while (lease.epoch && (previous & ~(kEpochStep - 1)) == lease.epoch && (previous & kJobMask)) {
    if (state.compare_exchange_weak(previous, previous & ~(kReleased | kJobMask | kReady))) {
      tailnet_service_changed();
      return;
    }
  }
}
extern "C" bool tailnet_service_permitted(void) {
  const uint32_t current = state.load();
  return !(current & kReading) && (current & kJobMask) && (current & kReady);
}
extern "C" bool tailnet_display_park(void) {
  const uint32_t current = state.load();
  return !(current & kReading) && (current & kJobMask) && (current & kReleased) && (current & kReady);
}
extern "C" void tailnet_display_unpark(void) {
  // Runtime owns display restoration after transport teardown (F11).
}
extern "C" tailnet_service_job_t tailnet_service_job(void) {
  const uint32_t current = state.load();
  return !(current & kReading) && (current & kReady) ? static_cast<tailnet_service_job_t>((current & kJobMask) >> 2)
                                                     : TAILNET_JOB_NONE;
}

extern "C" bool tailnet_service_activate(tailnet_service_lease_t lease, bool released) {
  uint32_t previous = state.load();
  while (lease.epoch && (previous & ~(kEpochStep - 1)) == lease.epoch && (previous & kJobMask) &&
         !(previous & kReading)) {
    if (state.compare_exchange_weak(previous, previous | kReady | (released ? kReleased : 0))) {
      tailnet_service_changed();
      return true;
    }
  }
  return false;
}

extern "C" tailnet_service_lease_t tailnet_service_acquire(tailnet_service_job_t job, bool released) {
  return acquire(job, released, true);
}
extern "C" tailnet_service_lease_t tailnet_service_prepare(tailnet_service_job_t job) {
  return acquire(job, false, false);
}

extern "C" void tailnet_service_deactivate(tailnet_service_lease_t lease) {
  uint32_t previous = state.load();
  while (lease.epoch && (previous & ~(kEpochStep - 1)) == lease.epoch && (previous & kJobMask)) {
    if (state.compare_exchange_weak(previous, previous & ~kReady)) {
      tailnet_service_changed();
      return;
    }
  }
}
