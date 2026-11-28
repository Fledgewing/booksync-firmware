// SPDX-License-Identifier: GPL-3.0-or-later
#include "ClockRuntime.h"

#include <TrustedTime.h>
#include <Logging.h>
#include <ctime>

#include "booksync/services/Clock.h"

#include "booksync/services/OfflineLibrary.h"
namespace booksync {
void publishRuntimeClock() {
  trustedtime::processSync();
  sampleRuntimeClock();
}
void sampleRuntimeClock() {
  ClockStatus value{};
  switch (trustedtime::clockStatus()) {
    case trustedtime::ClockSource::Sntp:
      value.evidence = ClockEvidence::Synchronized;
      break;
    case trustedtime::ClockSource::Rtc:
      value.evidence = ClockEvidence::LiveRtc;
      break;
    case trustedtime::ClockSource::Retained:
      value.evidence = ClockEvidence::Retained;
      break;
    case trustedtime::ClockSource::Unknown:
      break;
  }
  if (value.known()) {
    static bool invalidReported = false;
    const auto now = std::time(nullptr);
    std::tm civil{};
    if (localtime_r(&now, &civil)) {
      ClockDateTime local{static_cast<uint16_t>(civil.tm_year + 1900), static_cast<uint8_t>(civil.tm_mon + 1),
                          static_cast<uint8_t>(civil.tm_mday), 0, static_cast<uint8_t>(civil.tm_hour),
                          static_cast<uint8_t>(civil.tm_min), static_cast<uint8_t>(civil.tm_sec)};
      value = projectClock(value.evidence, static_cast<int64_t>(now), local);
    } else {
      value = {};
    }
    if (!value.hasDateTime() && !invalidReported) LOG_ERR("BSC", "Known clock has invalid date/time");
    invalidReported = !value.hasDateTime();
  }
  publishClockStatus(value);
}
}  // namespace booksync
