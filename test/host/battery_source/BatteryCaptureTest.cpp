// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include <vector>

#include "booksync/services/BatteryCapture.h"
using namespace booksync::battery;
namespace {
struct Sink {
  std::vector<Record> rows;
  bool fail = false;
  static bool append(void* p, const Record& r) {
    auto& s = *static_cast<Sink*>(p);
    if (s.fail) return false;
    s.rows.push_back(r);
    return true;
  }
  CaptureSink port() { return {this, append}; }
};
CaptureInput input(unsigned second = 0) {
  return {1795600000 + second, second, 80, 0, 0, 0, true, true, false, false, false, false};
}
}  // namespace
TEST(BatteryCapture, CheckedZeroIsCapturedAndUnknownTelemetryIsAGap) {
  Capture c;
  Sink s;
  auto in = input();
  in.pct = 0;
  c.tick(in, s.port());
  ASSERT_EQ(s.rows.size(), 1u);
  EXPECT_EQ(s.rows.back().pct, 0);
  EXPECT_EQ(s.rows.back().kind, KIND_BOOT);
  in.known = false;
  ++in.uptime;
  ++in.epoch;
  c.tick(in, s.port());
  ASSERT_EQ(s.rows.size(), 2u);
  EXPECT_EQ(s.rows.back().kind, KIND_GAP);
  for (unsigned i = 0; i < 20; ++i) c.tick(in, s.port());
  EXPECT_EQ(s.rows.size(), 2u);
}
TEST(BatteryCapture, UnknownClockNeverInventsElapsedBatteryTime) {
  Capture c;
  Sink s;
  auto in = input();
  in.clockKnown = false;
  c.tick(in, s.port());
  ASSERT_EQ(s.rows.size(), 1u);
  EXPECT_EQ(s.rows[0].kind, KIND_GAP);
  EXPECT_EQ(s.rows[0].t, 0u);
  in = input(3600);
  c.tick(in, s.port());
  ASSERT_EQ(s.rows.size(), 2u);
  EXPECT_EQ(s.rows[1].kind, KIND_BOOT);
}
TEST(BatteryCapture, NativeTransitionsAndPeriodicSamplesAreDistinctFromPaint) {
  Capture c;
  Sink s;
  auto in = input();
  c.tick(in, s.port());
  in = input(1);
  c.tick(in, s.port());
  EXPECT_EQ(s.rows.size(), 1u);
  in = input(60);
  c.tick(in, s.port());
  EXPECT_EQ(s.rows.back().kind, KIND_LEVEL);
  in = input(61);
  in.content = CONTENT_TEXT;
  c.tick(in, s.port());
  EXPECT_EQ(s.rows.back().kind, KIND_CONTENT);
  ++in.uptime;
  ++in.epoch;
  in.light = true;
  in.lightLevel = 40;
  c.tick(in, s.port());
  EXPECT_EQ(s.rows.back().kind, KIND_LIGHT);
  ++in.uptime;
  ++in.epoch;
  in.radio = true;
  c.tick(in, s.port());
  EXPECT_EQ(s.rows.back().kind, KIND_RADIO);
  ++in.uptime;
  ++in.epoch;
  in.usb = true;
  c.tick(in, s.port());
  EXPECT_EQ(s.rows.back().kind, KIND_CHARGE);
}
TEST(BatteryCapture, AClockJumpSeparatesMeasurementsWithAnExplicitGap) {
  Capture c;
  Sink s;
  c.tick(input(), s.port());
  auto in = input(60);
  in.epoch += 3600;
  c.tick(in, s.port());
  ASSERT_EQ(s.rows.size(), 2u);
  EXPECT_EQ(s.rows.back().kind, KIND_GAP);
  ++in.uptime;
  ++in.epoch;
  c.tick(in, s.port());
  ASSERT_EQ(s.rows.size(), 3u);
  EXPECT_EQ(s.rows.back().kind, KIND_BOOT);
}
TEST(BatteryCapture, FailedPublicationRetainsTheExactRecordAndBreaksTheLostInterval) {
  Capture c;
  Sink s;
  auto in = input();
  s.fail = true;
  c.tick(in, s.port());
  EXPECT_TRUE(c.pending());
  EXPECT_TRUE(s.rows.empty());
  s.fail = false;
  in = input(60);
  c.tick(in, s.port());
  ASSERT_EQ(s.rows.size(), 1u);
  EXPECT_EQ(s.rows[0].t, input().epoch);
  EXPECT_FALSE(c.pending());
  c.tick(in, s.port());
  ASSERT_EQ(s.rows.size(), 2u);
  EXPECT_EQ(s.rows.back().kind, KIND_GAP);
  c.tick(in, s.port());
  EXPECT_EQ(s.rows.back().kind, KIND_BOOT);
}
TEST(BatteryCapture, SleepAndRestartUseNativeLifecycleEventsWithoutANewTimer) {
  for (auto kind : {KIND_SLEEP, KIND_RESTART}) {
    Capture c;
    Sink s;
    c.tick(input(), s.port());
    auto in = input(5);
    c.tick(in, s.port(), kind);
    ASSERT_EQ(s.rows.size(), 2u);
    EXPECT_EQ(s.rows.back().kind, kind);
    EXPECT_EQ(s.rows.back().t, in.epoch);
  }
}

TEST(BatteryCapture, LostStorageOwnershipBreaksTheUnobservedInterval) {
  Capture c;
  Sink s;
  c.tick(input(), s.port());
  c.pause();
  c.tick(input(60), s.port());
  ASSERT_EQ(s.rows.size(), 2u);
  EXPECT_EQ(s.rows.back().kind, KIND_GAP);
  c.tick(input(61), s.port());
  EXPECT_EQ(s.rows.back().kind, KIND_BOOT);
}
