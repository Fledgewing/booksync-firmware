#include <gtest/gtest.h>

#include <cmath>

#include "booksync/services/Battery.h"
using namespace booksync::battery;
namespace {
constexpr uint32_t Base = 1795600000;
Record point(uint32_t t, uint8_t kind, uint8_t pct, uint8_t flags = 0) {
  Record r;
  r.t = t;
  r.kind = kind;
  r.pct = pct;
  r.flags = flags;
  return r;
}
void normal(Analyzer& a, uint8_t last = 78) {
  a.add(point(Base, KIND_BOOT, 100));
  a.add(point(Base + 3600, KIND_CONTENT, 96, CONTENT_TEXT << FLAG_CONTENT_SHIFT));
  a.add(point(Base + 7200, KIND_LEVEL, 92, CONTENT_TEXT << FLAG_CONTENT_SHIFT));
  a.add(point(Base + 10800, KIND_SLEEP, 88));
  a.add(point(Base + 86400, KIND_BOOT, last));
  a.finish();
}
TEST(BatteryAnalysis, MeasuredReadingAndStandbyProduceTheActualUsualUseEstimate) {
  Analyzer a({}, Base + 86400, nullptr, nullptr, nullptr);
  normal(a);
  const auto s = summarize(a);
  ASSERT_TRUE(s.reading);
  ASSERT_TRUE(s.standby);
  ASSERT_TRUE(s.timeLeft);
  EXPECT_NEAR(s.readingPctH, 4, 0.0001);
  EXPECT_NEAR(s.standbyPctH, 10.0 / 21, 0.0001);
  EXPECT_NEAR(s.timeLeftH, 78.0 / (3.0 / 24 * 4 + 21.0 / 24 * 10.0 / 21), 0.001);
  EXPECT_NEAR(s.dataHours, 24, 0.001);
}
TEST(BatteryAnalysis, ACheckedZeroIsAZeroEstimateAfterMeasuredUse) {
  Analyzer a({}, Base + 86400, nullptr, nullptr, nullptr);
  normal(a, 0);
  auto s = summarize(a);
  EXPECT_EQ(s.pct, 0);
  EXPECT_TRUE(s.timeLeft);
  EXPECT_FLOAT_EQ(s.timeLeftH, 0);
}
TEST(BatteryAnalysis, UnknownHistoryHasNoRatesOrDuration) {
  Analyzer a({}, Base, nullptr, nullptr, nullptr);
  a.finish();
  auto s = summarize(a);
  EXPECT_FALSE(s.standby);
  EXPECT_FALSE(s.reading);
  EXPECT_FALSE(s.timeLeft);
}
TEST(BatteryAnalysis, RateThresholdRequiresOneHourAndMoreThanTwoPercentChange) {
  for (unsigned mode = 0; mode < 2; ++mode) {
    Analyzer a({}, Base + 3600, nullptr, nullptr, nullptr);
    a.add(point(Base, KIND_CONTENT, 90, CONTENT_TEXT << FLAG_CONTENT_SHIFT));
    a.add(point(Base + (mode ? 3600 : 1800), KIND_LEVEL, mode ? 88 : 80, CONTENT_TEXT << FLAG_CONTENT_SHIFT));
    a.finish();
    EXPECT_FALSE(summarize(a).reading);
  }
}
TEST(BatteryAnalysis, NegativeOrFlatDrainNeverBecomesAMeasuredRate) {
  for (uint8_t end : {uint8_t(90), uint8_t(95)}) {
    Analyzer a({}, Base + 3600, nullptr, nullptr, nullptr);
    a.add(point(Base, KIND_CONTENT, 90, CONTENT_TEXT << FLAG_CONTENT_SHIFT));
    a.add(point(Base + 3600, KIND_LEVEL, end, CONTENT_TEXT << FLAG_CONTENT_SHIFT));
    a.finish();
    EXPECT_FALSE(summarize(a).reading);
  }
}
TEST(BatteryAnalysis, SevenDayShareClipsTheActualIntervalAtTheWindowBoundary) {
  Analyzer a({}, Base + 40 * 86400, nullptr, nullptr, nullptr);
  a.add(point(Base, KIND_SLEEP, 90));
  a.add(point(Base + 40 * 86400, KIND_BOOT, 70));
  a.finish();
  EXPECT_DOUBLE_EQ(a.recentStandbyS, 7 * 86400.0);
}
TEST(BatteryAnalysis, ChargingWhileAsleepDoesNotDonateAStandbyRate) {
  Analyzer a({}, Base + 86400, nullptr, nullptr, nullptr);
  a.add(point(Base, KIND_SLEEP, 70));
  a.add(point(Base + 86400, KIND_BOOT, 95, FLAG_USB));
  a.finish();
  EXPECT_FALSE(a.rate(CAT_STANDBY).has());
  EXPECT_EQ(a.recentStandbyS, 0);
}
TEST(BatteryAnalysis, CrashesAndBackwardClocksDoNotConnectUnknownIntervals) {
  for (bool crash : {false, true}) {
    Analyzer a({}, Base + 86400, nullptr, nullptr, nullptr);
    a.add(point(Base, KIND_CONTENT, 90, CONTENT_TEXT << FLAG_CONTENT_SHIFT));
    a.add(point(crash ? Base + 86400 : Base - 1, KIND_BOOT, 70, crash ? RESET_CRASH << FLAG_RESET_SHIFT : 0));
    a.finish();
    EXPECT_FALSE(a.rate(CAT_READING).has());
  }
}
TEST(BatteryAnalysis, AGapRecordRetiresTheOpenMeasurement) {
  Analyzer a({}, Base + 7200, nullptr, nullptr, nullptr);
  a.add(point(Base, KIND_CONTENT, 90, CONTENT_TEXT << FLAG_CONTENT_SHIFT));
  a.add(point(Base + 3600, KIND_GAP, 80));
  a.add(point(Base + 7200, KIND_BOOT, 70));
  a.finish();
  EXPECT_FALSE(a.rate(CAT_READING).has());
  EXPECT_FALSE(a.rate(CAT_AWAKE).has());
}
TEST(BatteryAnalysis, ApproximateBootTimeCannotInventRecentCalendarCoverage) {
  Analyzer a({}, Base + 86400, nullptr, nullptr, nullptr);
  auto start = point(100, KIND_CONTENT, 90, FLAG_APPROX_TIME | (CONTENT_TEXT << FLAG_CONTENT_SHIFT));
  start.era = 7;
  auto end = start;
  end.kind = KIND_LEVEL;
  end.t = 7300;
  end.pct = 80;
  a.add(start);
  a.add(end);
  a.finish();
  EXPECT_TRUE(summarize(a).reading);
  EXPECT_EQ(a.recentAwakeS, 0);
  EXPECT_EQ(a.days, 0u);
}
TEST(BatteryAnalysis, AnEraTransitionDoesNotBridgeApproximateBoots) {
  Analyzer a({}, 10000, nullptr, nullptr, nullptr);
  auto start = point(100, KIND_CONTENT, 90, FLAG_APPROX_TIME | (CONTENT_TEXT << FLAG_CONTENT_SHIFT));
  auto end = start;
  start.era = 7;
  end.era = 8;
  end.t = 7300;
  end.pct = 80;
  a.add(start);
  a.add(end);
  a.finish();
  EXPECT_FALSE(a.rate(CAT_READING).has());
}
TEST(BatteryAnalysis, ExactLinearSamplesRetainAKnownZeroStandardError) {
  Analyzer a({}, Base + 86400, nullptr, nullptr, nullptr);
  normal(a);
  double se = -1;
  ASSERT_TRUE(a.rate(CAT_READING).se(se));
  EXPECT_DOUBLE_EQ(se, 0);
}
TEST(BatteryAnalysis, ContentLightAndRadioCategoriesUseMeasuredStates) {
  Config c;
  c.hasLight = true;
  Analyzer a(c, Base + 3600, nullptr, nullptr, nullptr);
  const uint8_t flags = (CONTENT_XTC << FLAG_CONTENT_SHIFT) | FLAG_LIGHT | FLAG_RADIO;
  a.add(point(Base, KIND_CONTENT, 90, flags));
  a.add(point(Base + 3600, KIND_LEVEL, 84, flags));
  a.finish();
  EXPECT_NEAR(a.rate(CAT_READING_XTC).pctPerHour(false), 6, 0.0001);
  EXPECT_TRUE(a.rate(CAT_READING_LIGHT_ON).has());
  EXPECT_TRUE(a.rate(CAT_READING_RADIO_ON).has());
  EXPECT_FALSE(a.rate(CAT_READING_TEXT).has());
  EXPECT_FALSE(a.rate(CAT_READING_LIGHT_OFF).has());
}
TEST(BatteryAnalysis, CodecPreservesEveryNativeCounterWithoutUnalignedLoads) {
  Record r = point(Base, KIND_LIGHT, 0, FLAG_LIGHT);
  r.era = 9;
  r.mv = 3741;
  r.lightLevel = 37;
  r.full = 255;
  r.pages = 65535;
  r.partial = 12345;
  uint8_t bytes[RECORD_SIZE + 1]{};
  encode(r, bytes + 1);
  auto got = decode(bytes + 1);
  EXPECT_EQ(got.t, r.t);
  EXPECT_EQ(got.pct, 0);
  EXPECT_EQ(got.mv, 3741);
  EXPECT_EQ(got.pages, 65535);
  EXPECT_EQ(got.partial, 12345);
  EXPECT_EQ(got.full, 255);
  EXPECT_EQ(got.era, 9);
}
}  // namespace

TEST(BatteryAnalysis, UsualUsePoolsMeasuredAwakeAndReadingInsteadOfSubstitutingReadingRate) {
  Analyzer a({}, Base + 86400, nullptr, nullptr, nullptr);
  a.add(point(Base, KIND_BOOT, 100));
  a.add(point(Base + 3600, KIND_CONTENT, 92, CONTENT_TEXT << FLAG_CONTENT_SHIFT));
  a.add(point(Base + 7200, KIND_SLEEP, 88));
  a.add(point(Base + 86400, KIND_BOOT, 77));
  a.finish();
  const auto s = summarize(a);
  ASSERT_TRUE(s.screenOn);
  ASSERT_TRUE(s.timeLeft);
  EXPECT_NEAR(s.screenOnPctH, 6, 0.0001);
  EXPECT_NEAR(s.readingPctH, 4, 0.0001);
  EXPECT_NEAR(s.timeLeftH, 77.0 / (2.0 / 24 * 6 + 22.0 / 24 * 0.5), 0.001);
}
TEST(BatteryAnalysis, MeasuredAwakeUseCanEstimateUsualWithoutInventingAReadingRate) {
  Analyzer a({}, Base + 86400, nullptr, nullptr, nullptr);
  a.add(point(Base, KIND_BOOT, 100));
  a.add(point(Base + 7200, KIND_SLEEP, 92));
  a.add(point(Base + 86400, KIND_BOOT, 81));
  a.finish();
  const auto s = summarize(a);
  EXPECT_FALSE(s.reading);
  ASSERT_TRUE(s.screenOn);
  ASSERT_TRUE(s.timeLeft);
  EXPECT_NEAR(s.screenOnPctH, 4, 0.0001);
}

TEST(BatteryAnalysis, BootWithoutACapturedSleepCannotAssignTheLostTimeToAwakeUse) {
  Analyzer a({}, Base + 86400, nullptr, nullptr, nullptr);
  a.add(point(Base, KIND_BOOT, 90));
  a.add(point(Base + 86400, KIND_BOOT, 70));
  a.finish();
  EXPECT_FALSE(a.rate(CAT_AWAKE).has());
  EXPECT_EQ(a.recentAwakeS, 0);
}
TEST(BatteryAnalysis, ExternalPowerAtSleepDoesNotProduceAStandbyDrainWhenUnpluggedLater) {
  Analyzer a({}, Base + 86400, nullptr, nullptr, nullptr);
  a.add(point(Base, KIND_SLEEP, 90, FLAG_USB));
  a.add(point(Base + 86400, KIND_BOOT, 80));
  a.finish();
  EXPECT_FALSE(a.rate(CAT_STANDBY).has());
  EXPECT_EQ(a.recentStandbyS, 0);
}
TEST(BatteryAnalysis, AnUnavailableCurrentSampleCannotBecomeAZeroHourEstimate) {
  Analyzer a({}, Base + 86401, nullptr, nullptr, nullptr);
  normal(a);
  a.add(point(Base + 86401, KIND_GAP, 0));
  a.finish();
  auto s = summarize(a);
  EXPECT_FALSE(s.pctKnown);
  EXPECT_FALSE(s.timeLeft);
  EXPECT_TRUE(s.reading);
  EXPECT_TRUE(s.standby);
}
