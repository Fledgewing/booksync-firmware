// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdio>
#include <cstdlib>

#include "booksync/import/ReadingPositions.h"

inline void runReadingPositionsScenario() {
  if (!std::getenv("BOOKSYNC_READING_POSITIONS_SCENARIO")) return;
  using namespace booksync;
  using namespace booksync::storage;
  using namespace booksync::importer;
  constexpr SourceIdentity source{{{1}}, {{2}}, {{3}}};
  LifecycleJournal journal({"/positions", "/positions.tmp", "/positions.bak"});
  static ReadingPositions worker(journal);
  bool ok = journal.recover() == JournalResult::Ok;
  const auto before = journal.pending();
  PositionStatus status;
  uint64_t primary = 0, backup = 0;
  ok = ok &&
       worker.retain("/legacy/progress.bin", PositionFormat::Epub, source, status, primary) == JournalResult::Ok &&
       status == PositionStatus::Supported;
  ok = ok &&
       worker.retain("/legacy/progress.bin.bak", PositionFormat::Epub, source, status, backup) == JournalResult::Ok;
  static char p[4992], b[4992];
  JournalEntry a, c;
  PositionEvidence pe{}, be{};
  ok = ok && journal.read(primary, a, p, sizeof(p)) == JournalResult::Ok && ReadingPositions::decode(a.payload, pe) &&
       journal.read(backup, c, b, sizeof(b)) == JournalResult::Ok && ReadingPositions::decode(c.payload, be);
  reader::LocatorView locator{};
  reader::Sample sample{};
  ok = ok && ReadingPositions::select(&pe, &be, locator, sample) && sample.sourceOffset == 144 &&
       sample.primaryBytes == 20 && sample.backupBytes == 20 && !sample.hasStats && a.acknowledged == 0;
  uint64_t id = 0;
  ok = ok && worker.retain("/legacy/torn.bin", PositionFormat::Epub, source, status, id) == JournalResult::Ok &&
       status == PositionStatus::Corrupt;
  ok = ok && worker.retain("/legacy/future.bin", PositionFormat::Epub, {}, status, id) == JournalResult::Ok &&
       status == PositionStatus::Unsupported;
  pe.raw = "torn";
  ok = ok && ReadingPositions::select(&pe, &be, locator, sample) && sample.sourceOffset == 96;
  be.source.edition.bytes[0] = 8;
  ok = ok && !ReadingPositions::select(&pe, &be, locator, sample);
  ok = ok && journal.pending() == 4 && (before == 0 || before == 4);
  std::printf(
      "BookSync reading positions scenario %s: two-boot evidence, raw anchor, backup, unresolved; workspace=%zu\n",
      ok ? "PASS" : "FAIL", sizeof(worker));
  std::fflush(nullptr);
  std::_Exit(ok ? 0 : 1);
}
