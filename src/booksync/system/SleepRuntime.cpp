// SPDX-License-Identifier: GPL-3.0-or-later
#include "SleepRuntime.h"

#include <Logging.h>

#include <optional>

#include "NetworkPhase.h"
#include "activities/Activity.h"
#include "booksync/services/OfflineLibrary.h"
#include "booksync/services/Sleep.h"
#include "booksync/ui/Sleep.h"
namespace booksync {
namespace {
SleepCoordinator coordinator;
SleepPresentation presentation;
bool active = false;
}  // namespace
SleepReadiness prepareSleep(uint32_t now) {
  if (!active) {
    coordinator.begin(now, [] { activityManager.prepareForSleep(); });
    {
      std::optional<RenderLock> lock;
      if (!RenderLock::heldByCurrentTask()) lock.emplace();
      publishSleepView(coordinator.sleepState());
      presentation.adoptSleep(offlineLibrary());
    }
    active = true;
  }
  // finish revokes permitted() before waiting on tokens. Repeated calls cannot
  // start a worker, steal its files or release a leased C3 framebuffer early.
  const bool released = networkAdmission().finish();
  const auto step = coordinator.poll(now, released && networkDisplayReady());
  if (step == SleepStep::Ready && presentation.retainNative(coordinator.sleepState().generation))
    return SleepReadiness::Ready;
  if (step == SleepStep::Draining) return SleepReadiness::Draining;
  LOG_ERR("SLEEP", "Sleep drain deadline expired; retaining live I/O ownership and deferring sleep");
  retireSleep();
  return SleepReadiness::Expired;
}
void retireSleep() {
  activityManager.retireSleepCapture();
  coordinator.wake(coordinator.sleepState().generation);
  presentation.retireSleepPresentation();
  std::optional<RenderLock> lock;
  if (!RenderLock::heldByCurrentTask()) lock.emplace();
  publishSleepView(coordinator.sleepState());
  active = false;
}
}  // namespace booksync
