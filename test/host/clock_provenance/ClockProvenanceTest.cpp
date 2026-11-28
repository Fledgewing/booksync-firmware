#include <gtest/gtest.h>

#include "ClockProvenance.h"
using namespace trustedtime;
constexpr int64_t Now = 1795813200;
TEST(ClockProvenance, ColdBootPlausibleFloorAndHistoricalFlagsAreUnknown) {
  ClockProvenance p;
  p.boot(false, {ClockSource::Sntp, Now}, Now + 600);
  EXPECT_EQ(p.status(Now + 600), ClockSource::Unknown);
}
TEST(ClockProvenance, RetainedKnownClockRequiresDeepSleepAndRunningClock) {
  ClockProvenance p;
  p.boot(true, {ClockSource::Sntp, Now}, Now + 600);
  EXPECT_EQ(p.status(Now + 600), ClockSource::Retained);
  p.boot(true, {ClockSource::Sntp, Now}, 0);
  EXPECT_EQ(p.status(0), ClockSource::Unknown);
  p.boot(true, {ClockSource::Sntp, Now}, Now - 1);
  EXPECT_EQ(p.status(Now), ClockSource::Unknown);
}
TEST(ClockProvenance, RtcAcceptanceRequiresValidatedLiveRead) {
  ClockProvenance p;
  p.rtc(false, Now);
  EXPECT_EQ(p.status(Now), ClockSource::Unknown);
  p.rtc(true, Now);
  EXPECT_EQ(p.status(Now), ClockSource::Rtc);
}
TEST(ClockProvenance, FailedSntpAndWifiDoNotUpgradeClock) {
  ClockProvenance p;
  p.sntp(false, Now);
  EXPECT_EQ(p.status(Now), ClockSource::Unknown);
  p.sntp(true, Now);
  EXPECT_EQ(p.status(Now), ClockSource::Sntp);
  p.sntp(false, Now + 60);
  EXPECT_EQ(p.status(Now + 60), ClockSource::Sntp);
}
TEST(ClockProvenance, InvalidRunningClockRevokesProvenanceAndRetention) {
  ClockProvenance p;
  p.sntp(true, Now);
  EXPECT_EQ(p.status(0), ClockSource::Unknown);
  EXPECT_EQ(p.marker().source, ClockSource::Unknown);
  EXPECT_EQ(p.status(Now), ClockSource::Unknown);
}
TEST(ClockProvenance, UnknownRetainedMarkerCannotBecomeKnown) {
  ClockProvenance p;
  p.boot(true, {ClockSource::Unknown, Now}, Now + 60);
  EXPECT_EQ(p.status(Now + 60), ClockSource::Unknown);
}
