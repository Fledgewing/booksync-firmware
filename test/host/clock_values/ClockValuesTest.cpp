#include <gtest/gtest.h>
#include <limits>
#include "booksync/services/Clock.h"
using namespace booksync;
// Independent UTC fixtures: 2026-11-27 14:32:00 and 2028-02-29 00:00:00.
constexpr int64_t November = 1795789920, Leap = 1835395200;
TEST(ClockValues, UnknownNeverPublishesPlausibleNativeFloor) {
  const auto value = projectClock(ClockEvidence::Unknown, November, {2026, 11, 27, 0, 14, 32, 0});
  EXPECT_FALSE(value.known()); EXPECT_FALSE(value.hasDateTime()); EXPECT_EQ(value.utcSeconds, 0);
  EXPECT_EQ(value.local.year, 0);
}
TEST(ClockValues, EveryKnownProvenanceRetainsUtcAndLocalCalendar) {
  for (auto evidence : {ClockEvidence::Synchronized, ClockEvidence::LiveRtc, ClockEvidence::Retained}) {
    const auto value = projectClock(evidence, November, {2026, 11, 27, 0, 14, 32, 0});
    EXPECT_EQ(value.evidence, evidence); EXPECT_TRUE(value.hasDateTime()); EXPECT_EQ(value.utcSeconds, November);
    EXPECT_EQ(value.local.weekday, 5); EXPECT_EQ(value.utcOffsetSeconds, 0);
  }
}
TEST(ClockValues, KnownMidnightIsNotAbsence) {
  const auto value = projectClock(ClockEvidence::LiveRtc, Leap, {2028, 2, 29});
  EXPECT_TRUE(value.hasDateTime()); EXPECT_EQ(value.local.hour, 0); EXPECT_EQ(value.local.weekday, 2);
}
TEST(ClockValues, PositiveNegativeAndFractionalTimezoneOffsetsRemainExplicit) {
  const auto west = projectClock(ClockEvidence::Synchronized, November, {2026, 11, 27, 0, 6, 32, 0});
  EXPECT_EQ(west.utcOffsetSeconds, -28800); EXPECT_TRUE(west.hasDateTime());
  const auto east = projectClock(ClockEvidence::Synchronized, November, {2026, 11, 27, 0, 20, 17, 0});
  EXPECT_EQ(east.utcOffsetSeconds, 20700); EXPECT_TRUE(east.hasDateTime());
}
TEST(ClockValues, DateBoundaryAndChangedOffsetAreReprojected) {
  const auto a = projectClock(ClockEvidence::Retained, Leap, {2028, 2, 28, 0, 16, 0, 0});
  const auto b = projectClock(ClockEvidence::Retained, Leap, {2028, 2, 28, 0, 17, 0, 0});
  EXPECT_EQ(a.local.weekday, 1); EXPECT_EQ(a.utcOffsetSeconds, -28800); EXPECT_EQ(b.utcOffsetSeconds, -25200);
  EXPECT_NE(a.utcOffsetSeconds, b.utcOffsetSeconds);
}
TEST(ClockValues, InvalidCivilFieldsNeverBecomeKnown) {
  for (ClockDateTime bad : {ClockDateTime{2026, 2, 29}, {2100, 2, 29}, {2026, 0, 1}, {2026, 13, 1},
       {2026, 11, 0}, {2026, 11, 31}, {2026, 11, 27, 0, 24}, {2026, 11, 27, 0, 14, 60},
       {2026, 11, 27, 0, 14, 32, 60}})
    EXPECT_FALSE(projectClock(ClockEvidence::LiveRtc, November, bad).known());
}
TEST(ClockValues, InvalidEpochAndMismatchedDateCannotOverflowOrPublish) {
  for (auto bad : {int64_t{0}, int64_t{-1}, std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max()})
    EXPECT_FALSE(projectClock(ClockEvidence::LiveRtc, bad, {2026, 11, 27}).known());
  EXPECT_FALSE(projectClock(ClockEvidence::LiveRtc, November, {2028, 2, 29}).known());
  EXPECT_FALSE(projectClock(static_cast<ClockEvidence>(255), November, {2026, 11, 27}).known());
}
