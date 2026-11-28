// SPDX-License-Identifier: GPL-3.0-or-later
#include "ClockRuntime.h"

#include <TrustedTime.h>

#include "booksync/services/OfflineLibrary.h"
namespace booksync {
void publishRuntimeClock() {
  trustedtime::processSync();
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
  publishClockStatus(value);
}
}  // namespace booksync
