// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>
#include <TrustedTime.h>

#include <cstdio>
#include <cstdlib>

#include "booksync/services/OfflineLibrary.h"
#include "booksync/system/ClockRuntime.h"
inline void runClockProvenanceScenario() {
  if (!std::getenv("BOOKSYNC_CLOCK_SCENARIO")) return;
  constexpr int64_t Now = 1795813200;
  trustedtime::ClockProvenance fake;
  fake.boot(false, {trustedtime::ClockSource::Sntp, Now}, Now + 600);
  bool ok = fake.status(Now + 600) == trustedtime::ClockSource::Unknown;
  fake.boot(true, {trustedtime::ClockSource::Sntp, Now}, Now + 600);
  ok = ok && fake.status(Now + 600) == trustedtime::ClockSource::Retained;
  // This opt-in test uses the pinned simulator's successful SNTP stub. It never
  // changes host time or treats the date-shaped host epoch as provenance.
  ok = ok && trustedtime::syncNow(0);
  booksync::publishRuntimeClock();
  ok = ok && booksync::offlineLibrary().clockStatus().known();
  LOG_INF("BSC", "BookSync clock scenario %s: cold-floor=unknown retained=known synthetic-sntp=known",
          ok ? "PASS" : "FAIL");
  std::fflush(nullptr);
  std::_Exit(ok ? 0 : 1);
}
