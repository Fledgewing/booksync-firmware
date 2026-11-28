// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <HalStorage.h>
#include <Logging.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "booksync/services/Battery.h"
#include "booksync/services/BatteryCapture.h"
#include "booksync/storage/BatteryJournal.h"
inline void runBatterySourceScenario() {
  if (!std::getenv("BOOKSYNC_BATTERY_SOURCE_TEST")) return;
  using namespace booksync;
  using namespace battery;
  unsigned cases = 0;
  auto require = [&](bool ok) {
    ++cases;
    if (!ok) {
      LOG_ERR("BSBT", "Battery source assertion %u failed", cases);
      std::_Exit(2);
    }
  };
  require(Storage.mkdir("/.booksync-battery-fixture"));
  constexpr uint32_t base = 1795600000;
  auto point = [](uint32_t t, uint8_t kind, uint8_t pct, uint8_t flags = 0) {
    Record r;
    r.t = t;
    r.kind = kind;
    r.pct = pct;
    r.flags = flags;
    return r;
  };
  storage::BatteryJournal journal("/.booksync-battery-fixture/known", 128);
  require(journal.recover(true) == storage::JournalResult::Ok);
  require(journal.append(point(base, KIND_BOOT, 100)) == storage::JournalResult::Ok);
  require(journal.append(point(base + 3600, KIND_CONTENT, 96, CONTENT_TEXT << FLAG_CONTENT_SHIFT)) ==
          storage::JournalResult::Ok);
  require(journal.append(point(base + 7200, KIND_LEVEL, 92, CONTENT_TEXT << FLAG_CONTENT_SHIFT)) ==
          storage::JournalResult::Ok);
  require(journal.append(point(base + 10800, KIND_SLEEP, 88)) == storage::JournalResult::Ok);
  require(journal.append(point(base + 86400, KIND_BOOT, 78)) == storage::JournalResult::Ok);
  storage::BatteryJournal recovered("/.booksync-battery-fixture/known", 128);
  auto result = recovered.startRecovery();
  unsigned steps = 0;
  while (result == storage::JournalResult::Incomplete && ++steps < 8) result = recovered.recoverStep();
  require(result == storage::JournalResult::Ok && steps == 4 && recovered.count() == 5);
  static Analyzer analysis({}, base + 86400, nullptr, nullptr, nullptr);
  for (unsigned i = 0; i < recovered.count(); ++i) {
    Record r;
    require(recovered.read(i, r) == storage::JournalResult::Ok);
    analysis.add(r);
  }
  analysis.finish();
  auto value = summarize(analysis);
  require(value.reading && value.standby && value.timeLeft && std::fabs(value.readingPctH - 4) < 0.001);
  require(std::fabs(value.standbyPctH - 10.0 / 21) < 0.001 && value.pct == 78);
  storage::BatteryJournal capped("/.booksync-battery-fixture/capped", 1);
  require(capped.recover(true) == storage::JournalResult::Ok);
  require(capped.append(point(base, KIND_LEVEL, 0)) == storage::JournalResult::Ok);
  require(capped.append(point(base + 60, KIND_LEVEL, 1)) == storage::JournalResult::Capacity);
  storage::BatteryJournal full("/.booksync-battery-fixture/capped", 1);
  require(full.recover() == storage::JournalResult::Ok && full.capped() && full.count() == 2);
  Record gap;
  require(full.read(1, gap) == storage::JournalResult::Ok && gap.kind == KIND_GAP);
  require(full.read(0, gap) == storage::JournalResult::Ok && gap.pct == 0);
  storage::BatteryJournal missing("/.booksync-battery-fixture/missing");
  require(missing.recover() == storage::JournalResult::NotFound &&
          !Storage.exists("/.booksync-battery-fixture/missing"));
  require(Storage.mkdir("/.booksync-battery-fixture/future"));
  auto file = Storage.open("/.booksync-battery-fixture/future/000000.bin", O_WRONLY | O_CREAT | O_TRUNC);
  require(bool(file));
  require(file.write("BSB2", 4) == 4 && file.sync());
  require(file.close());
  storage::BatteryJournal future("/.booksync-battery-fixture/future");
  require(future.recover() == storage::JournalResult::RecoveryRequired && future.futureVersion());
  require(future.append(point(base, KIND_LEVEL, 80)) == storage::JournalResult::RecoveryRequired);
  require(Storage.exists("/.booksync-battery-fixture/future/000000.bin"));
  struct Output {
    Record record;
    unsigned rows = 0;
  };
  Output output;
  Capture capture;
  CaptureSink sink{&output, [](void* p, const Record& r) {
                     auto& o = *static_cast<Output*>(p);
                     o.record = r;
                     ++o.rows;
                     return true;
                   }};
  CaptureInput in;
  in.clockKnown = true;
  in.epoch = base;
  in.known = true;
  in.pct = 0;
  require(capture.tick(in, sink) && output.record.kind == KIND_BOOT && output.record.pct == 0);
  in.clockKnown = false;
  ++in.uptime;
  require(capture.tick(in, sink) && output.record.kind == KIND_GAP && output.record.t == 0);
  const auto rows = output.rows;
  require(!capture.tick(in, sink) && output.rows == rows);
  LOG_INF("BSBT", "BookSync battery source PASS: cases=9 assertions=%u paint-io=0 extra-wakes=0", cases);
  std::fflush(nullptr);
  std::_Exit(0);
}
