// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "booksync/services/Sleep.h"
#include "booksync/system/Admission.h"
#include "booksync/ui/Sleep.h"
using namespace booksync;
TEST(Sleep, CapturesBeforeDrainAndKeepsFirstDeadline) {
  SleepCoordinator sleep;
  unsigned captures = 0;
  EXPECT_TRUE(sleep.begin(100, [&] { ++captures; }));
  EXPECT_FALSE(sleep.begin(200, [&] { ++captures; }));
  EXPECT_EQ(captures, 1u);
  EXPECT_EQ(sleep.poll(100, false), SleepStep::Draining);
  EXPECT_EQ(sleep.poll(100 + SleepCoordinator::DrainMs - 1, false), SleepStep::Draining);
  EXPECT_EQ(sleep.poll(100 + SleepCoordinator::DrainMs, false), SleepStep::Expired);
  EXPECT_EQ(sleep.poll(9000, true), SleepStep::Inactive);
}
TEST(Sleep, ReleasedAtDeadlineSleepsWithoutStartingMoreWork) {
  SleepCoordinator sleep;
  sleep.begin(10, [] {});
  EXPECT_EQ(sleep.poll(10 + SleepCoordinator::DrainMs, true), SleepStep::Ready);
  EXPECT_EQ(sleep.poll(10000, true), SleepStep::Inactive);
}
TEST(Sleep, WrapAndRetryCannotReviveOldPresentation) {
  SleepCoordinator sleep;
  sleep.begin(UINT32_MAX - 10, [] {});
  const auto old = sleep.sleepState();
  EXPECT_EQ(sleep.poll(5, false), SleepStep::Draining);
  EXPECT_EQ(sleep.poll(SleepCoordinator::DrainMs, false), SleepStep::Expired);
  sleep.begin(10000, [] {});
  EXPECT_NE(sleep.sleepState().generation, old.generation);
  EXPECT_FALSE(sleep.wake(old.generation));
  EXPECT_TRUE(sleep.wake(sleep.sleepState().generation));
  EXPECT_EQ(sleep.poll(10001, true), SleepStep::Inactive);
}
class CachedLibrary : public Library {
 public:
  mutable unsigned reads = 0;
  SleepView value{};
  Capabilities capabilities() const override { return {}; }
  Snapshot snapshot() const override { return {}; }
  CommandStatus submit(Intent) override { return CommandStatus::Unavailable; }
  SleepView sleepView() const override {
    ++reads;
    return value;
  }
};
TEST(Sleep, PaintUsesOnlyAdoptedSnapshotAndWakeRetiresIt) {
  CachedLibrary library;
  library.value = {1, true};
  SleepPresentation presentation;
  presentation.adoptSleep(library);
  library.value = {2, true};
  for (unsigned i = 0; i < 20; ++i) EXPECT_TRUE(presentation.retainNative(1));
  EXPECT_EQ(library.reads, 1u);
  EXPECT_FALSE(presentation.retainNative(2));
  presentation.retireSleepPresentation();
  EXPECT_FALSE(presentation.retainNative(1));
}
TEST(Sleep, RebootHasNoPresentationOrDrainOwnership) {
  SleepCoordinator reboot;
  EXPECT_EQ(reboot.poll(10, true), SleepStep::Inactive);
  EXPECT_FALSE(reboot.sleepState().captured);
  EXPECT_FALSE(reboot.wake(0));
}

namespace {
struct SleepHardware {
  bool stopped = true;
  unsigned restores = 0;
  AdmissionPorts ports() {
    return {this, [](void*) { return MemorySnapshot{128 * 1024, 64 * 1024, 0}; }, [](void*) { return true; },
            [](void* p) {
              ++static_cast<SleepHardware*>(p)->restores;
              return true;
            },
            [](void* p) { return static_cast<SleepHardware*>(p)->stopped; }};
  }
};
}  // namespace
TEST(Sleep, DeadlineNeverReleasesLiveWorkerOrC3Display) {
  for (bool needsDisplay : {true, false}) {
    SleepHardware hardware;
    Admission admission(hardware.ports(), needsDisplay);
    admission.setReading(false);
    ASSERT_EQ(admission.begin(TAILNET_JOB_CATALOG), AdmissionResult::Admitted);
    auto worker = admission.acquire(Resource::Worker);
    ASSERT_TRUE(worker);
    SleepCoordinator sleep;
    sleep.begin(100, [] {});
    EXPECT_EQ(sleep.poll(100, admission.finish()), SleepStep::Draining);
    EXPECT_FALSE(admission.permitted());
    EXPECT_EQ(sleep.poll(100 + SleepCoordinator::DrainMs, admission.finish()), SleepStep::Expired);
    EXPECT_TRUE(admission.busy());
    EXPECT_EQ(admission.displayReady(), !needsDisplay);
    EXPECT_EQ(hardware.restores, 0u);
    EXPECT_EQ(admission.begin(TAILNET_JOB_CATALOG), AdmissionResult::Busy);
    worker.reset();
    EXPECT_TRUE(admission.finish());
    EXPECT_TRUE(admission.displayReady());
    EXPECT_EQ(hardware.restores, needsDisplay ? 1u : 0u);
  }
}
TEST(Sleep, FailedTransportStopCannotAuthorizeSleepAndRetryGetsFreshCapture) {
  SleepHardware hardware;
  Admission admission(hardware.ports(), true);
  admission.setReading(false);
  ASSERT_EQ(admission.begin(TAILNET_JOB_CATALOG), AdmissionResult::Admitted);
  hardware.stopped = false;
  SleepCoordinator sleep;
  unsigned captures = 0;
  sleep.begin(0, [&] { ++captures; });
  EXPECT_EQ(sleep.poll(SleepCoordinator::DrainMs, admission.finish()), SleepStep::Expired);
  EXPECT_FALSE(admission.displayReady());
  hardware.stopped = true;
  EXPECT_TRUE(admission.finish());
  sleep.begin(3000, [&] { ++captures; });
  EXPECT_EQ(captures, 2u);
  EXPECT_EQ(sleep.poll(3000, admission.finish()), SleepStep::Ready);
}

TEST(Sleep, WakeCancelsPassWithoutStealingWorkerOwnership) {
  SleepHardware hardware;
  Admission admission(hardware.ports(), true);
  admission.setReading(false);
  ASSERT_EQ(admission.begin(TAILNET_JOB_CATALOG), AdmissionResult::Admitted);
  auto worker = admission.acquire(Resource::Worker);
  ASSERT_TRUE(worker);
  SleepCoordinator sleep;
  sleep.begin(0, [] {});
  EXPECT_EQ(sleep.poll(1, admission.finish()), SleepStep::Draining);
  EXPECT_TRUE(sleep.wake(sleep.sleepState().generation));
  EXPECT_EQ(sleep.poll(2, false), SleepStep::Inactive);
  EXPECT_TRUE(admission.busy());
  EXPECT_FALSE(admission.displayReady());
  worker.reset();
  EXPECT_TRUE(admission.finish());
  EXPECT_TRUE(admission.displayReady());
}
