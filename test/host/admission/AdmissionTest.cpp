// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "booksync/system/Admission.h"
using namespace booksync;
namespace {
struct Fake {
  MemorySnapshot memory{128 * 1024, 64 * 1024, 1024 * 1024};
  bool parked = false, parkOk = true, restoreOk = true, stopped = true;
  int parks = 0, restores = 0, stops = 0;
  AdmissionPorts ports() {
    return {this, [](void* p) { return static_cast<Fake*>(p)->memory; },
            [](void* p) {
              auto& f = *static_cast<Fake*>(p);
              ++f.parks;
              return f.parked = f.parkOk;
            },
            [](void* p) {
              auto& f = *static_cast<Fake*>(p);
              ++f.restores;
              if (f.restoreOk) f.parked = false;
              return f.restoreOk;
            },
            [](void* p) {
              auto& f = *static_cast<Fake*>(p);
              ++f.stops;
              return f.stopped;
            }};
  }
};
}  // namespace
TEST(Admission, FailClosedAndNoAdmissionWhileReading) {
  Fake f;
  Admission a(f.ports(), true);
  EXPECT_EQ(a.begin(TAILNET_JOB_CATALOG), AdmissionResult::Reading);
  EXPECT_EQ(f.parks, 0);
  a.setReading(false);
  ASSERT_EQ(a.begin(TAILNET_JOB_CATALOG), AdmissionResult::Admitted);
  EXPECT_TRUE(f.parked);
  a.setReading(true);
  EXPECT_FALSE(a.permitted());
  EXPECT_FALSE(a.acquire(Resource::Tls));
  ASSERT_TRUE(a.finish());
  EXPECT_FALSE(f.parked);
  EXPECT_EQ(a.begin(TAILNET_JOB_CATALOG), AdmissionResult::Reading);
}
TEST(Admission, LargestInternalBlockIsIndependentOfTotalAndPsram) {
  Fake f;
  Admission a(f.ports(), false);
  a.setReading(false);
  f.memory.internalLargest = 4096;
  EXPECT_EQ(a.begin(TAILNET_JOB_TRANSFER), AdmissionResult::Memory);
  EXPECT_FALSE(tailnet_service_permitted());
  f.memory.internalLargest = Admission::minimumLargest;
  EXPECT_EQ(a.begin(TAILNET_JOB_TRANSFER), AdmissionResult::Admitted);
  EXPECT_TRUE(a.finish());
}
TEST(Admission, FailedAdmissionRestoresDisplayAndReleasesTransport) {
  Fake f;
  Admission a(f.ports(), true);
  a.setReading(false);
  f.memory.internalFree = 0;
  EXPECT_EQ(a.begin(TAILNET_JOB_CATALOG), AdmissionResult::Memory);
  EXPECT_EQ(f.parks, 1);
  EXPECT_EQ(f.restores, 1);
  EXPECT_TRUE(a.displayReady());
  EXPECT_FALSE(tailnet_service_permitted());
  f.memory.internalFree = 128 * 1024;
  EXPECT_EQ(a.begin(TAILNET_JOB_CATALOG), AdmissionResult::Admitted);
  EXPECT_TRUE(a.finish());
}
TEST(Admission, BoundedResourcesAndMoveOnlyLifetime) {
  Fake f;
  Admission a(f.ports(), false);
  a.setReading(false);
  ASSERT_EQ(a.begin(TAILNET_JOB_CATALOG), AdmissionResult::Admitted);
  auto tls = a.acquire(Resource::Tls);
  auto worker = a.acquire(Resource::Worker);
  auto catalog = a.acquire(Resource::Catalog);
  EXPECT_TRUE(tls);
  EXPECT_TRUE(worker);
  EXPECT_TRUE(catalog);
  EXPECT_FALSE(a.acquire(Resource::Tls));
  EXPECT_FALSE(a.acquire(Resource::Worker));
  EXPECT_FALSE(a.acquire(Resource::Cover));
  EXPECT_TRUE(tailnet_service_permitted());
  auto moved = std::move(tls);
  EXPECT_FALSE(tls);
  EXPECT_TRUE(moved);
  catalog.reset();
  auto cover = a.acquire(Resource::Cover);
  EXPECT_TRUE(cover);
  moved.reset();
  worker.reset();
  cover.reset();
  EXPECT_TRUE(a.finish());
  EXPECT_FALSE(tailnet_service_permitted());
}
TEST(Admission, DrainAndRestoreFailuresKeepDisplayAndOwnerBlocked) {
  Fake f;
  Admission a(f.ports(), true);
  a.setReading(false);
  ASSERT_EQ(a.begin(TAILNET_JOB_PAIRING), AdmissionResult::Admitted);
  f.stopped = false;
  EXPECT_FALSE(a.finish());
  EXPECT_FALSE(a.displayReady());
  EXPECT_EQ(f.restores, 0);
  EXPECT_FALSE(a.acquire(Resource::Tls));
  EXPECT_EQ(a.begin(TAILNET_JOB_TRANSFER), AdmissionResult::Busy);
  f.stopped = true;
  f.restoreOk = false;
  EXPECT_FALSE(a.finish());
  EXPECT_FALSE(a.displayReady());
  EXPECT_FALSE(tailnet_service_permitted());
  f.restoreOk = true;
  EXPECT_TRUE(a.finish());
  EXPECT_TRUE(a.displayReady());
  EXPECT_EQ(a.begin(TAILNET_JOB_TRANSFER), AdmissionResult::Admitted);
  EXPECT_TRUE(a.finish());
}
TEST(Admission, AllocationFailureAndLowMemoryRecheckReleaseReservations) {
  Fake f;
  Admission a(f.ports(), false);
  a.setReading(false);
  ASSERT_EQ(a.begin(TAILNET_JOB_TRANSFER), AdmissionResult::Admitted);
  f.memory.internalLargest = 1;
  EXPECT_FALSE(a.acquire(Resource::Worker));
  EXPECT_FALSE(a.acquire(Resource::Tls));
  f.memory.internalLargest = 64 * 1024;
  {
    auto token = a.acquire(Resource::Worker);
    EXPECT_TRUE(token);
  }
  EXPECT_TRUE(a.acquire(Resource::Worker));  // temporary releases immediately
  EXPECT_TRUE(a.finish());
}
TEST(Admission, ExternalLeaseCannotBeStolen) {
  Fake f;
  Admission a(f.ports(), true);
  a.setReading(false);
  auto lease = tailnet_service_acquire(TAILNET_JOB_UPDATE, false);
  ASSERT_NE(lease.epoch, 0u);
  EXPECT_EQ(a.begin(TAILNET_JOB_CATALOG), AdmissionResult::Busy);
  EXPECT_EQ(f.parks, 0);
  tailnet_service_release(lease);
  ASSERT_EQ(a.begin(TAILNET_JOB_CATALOG), AdmissionResult::Admitted);
  EXPECT_TRUE(a.finish());
}
TEST(Admission, PreparedLeaseCannotStartTransportBeforeDisplayProof) {
  tailnet_set_reading(false);
  auto lease = tailnet_service_prepare(TAILNET_JOB_CATALOG);
  ASSERT_NE(lease.epoch, 0u);
  EXPECT_FALSE(tailnet_service_permitted());
  EXPECT_FALSE(tailnet_display_park());
  EXPECT_FALSE(tailnet_service_acquire(TAILNET_JOB_TRANSFER, false).epoch);
  EXPECT_TRUE(tailnet_service_activate(lease, true));
  EXPECT_TRUE(tailnet_display_park());
  tailnet_set_reading(true);
  EXPECT_FALSE(tailnet_service_activate(lease, true));
  tailnet_set_reading(false);
  auto next = tailnet_service_prepare(TAILNET_JOB_TRANSFER);
  tailnet_service_release(lease);
  EXPECT_TRUE(tailnet_service_activate(next, false));
  tailnet_service_release(next);
}
TEST(Admission, FailedDisplayReleaseNeverStartsTransport) {
  Fake f;
  f.parkOk = false;
  Admission a(f.ports(), true);
  a.setReading(false);
  EXPECT_EQ(a.begin(TAILNET_JOB_CATALOG), AdmissionResult::Display);
  EXPECT_FALSE(tailnet_service_permitted());
  EXPECT_TRUE(a.displayReady());
  EXPECT_FALSE(a.busy());
  EXPECT_EQ(f.restores, 0);
}
