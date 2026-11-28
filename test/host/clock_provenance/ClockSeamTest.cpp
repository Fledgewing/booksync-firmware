#include <gtest/gtest.h>
#include <unistd.h>

#include <ctime>

#include "TrustedTime.h"
#include "esp_sntp.h"
static time_t fakeEpoch = 1795813200;
extern "C" time_t clockFakeTime(time_t* result) {
  if (result) *result = fakeEpoch;
  return fakeEpoch;
}
using namespace trustedtime;
TEST(ClockSeam, PlausibleColdClockAndFloorNoteNeverEstablishProvenance) {
  EXPECT_EXIT(
      {
        init();
        note();
        _exit(clockStatus() == ClockSource::Unknown ? 0 : 1);
      },
      ::testing::ExitedWithCode(0), "");
}
TEST(ClockSeam, WifiStartAndFailedSyncNeverEstablishProvenance) {
  EXPECT_EXIT(
      {
        init();
        startSync();
        const bool synced = syncNow(100);
        _exit(!synced && clockStatus() == ClockSource::Unknown ? 0 : 1);
      },
      ::testing::ExitedWithCode(0), "");
}
TEST(ClockSeam, SuccessfulCallbackEstablishesAndInvalidClockRevokes) {
  EXPECT_EXIT(
      {
        init();
        timeval value{};
        value.tv_sec = fakeEpoch;
        clockSyncCallback(&value);
        const bool known = clockStatus() == ClockSource::Sntp;
        fakeEpoch = 0;
        const bool invalid = clockStatus() == ClockSource::Unknown;
        fakeEpoch = 1795813200;
        _exit(known && invalid && clockStatus() == ClockSource::Unknown ? 0 : 1);
      },
      ::testing::ExitedWithCode(0), "");
}
TEST(ClockSeam, AcceptedRtcAndSuccessfulSynchronousResultEstablishProvenance) {
  EXPECT_EXIT(
      {
        init();
        adopt(fakeEpoch);
        const bool rtc = clockStatus() == ClockSource::Rtc;
        clockSntpStatus = SNTP_SYNC_STATUS_COMPLETED;
        _exit(rtc && syncNow(100) && clockStatus() == ClockSource::Sntp ? 0 : 1);
      },
      ::testing::ExitedWithCode(0), "");
}
