// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdio>
#include <cstdlib>
#include <utility>

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
  for (const char* path : {"/legacy/legacy-4.bin", "/legacy/legacy-6.bin", "/legacy/legacy-10.bin",
                           "/legacy-upgrade/.crosspoint/book_0123456789abcdef0123456789abcdef/progress.bin",
                           "/legacy-upgrade/.crosspoint/book_0123456789abcdef0123456789abcdef/progress.bin.bak"}) {
    ok = ok && worker.retain(path, PositionFormat::Epub, source, status, id) == JournalResult::Ok &&
         status == PositionStatus::Supported;
  }
  const std::pair<const char*, PositionFormat> provenance[]{
      {"/legacy-upgrade/.crosspoint/epub_305419896/progress-path.bin", PositionFormat::Path},
      {"/legacy-upgrade/.crosspoint/epub_305419896/links.bin", PositionFormat::Links},
      {"/legacy-upgrade/.crosspoint/book_0123456789abcdef0123456789abcdef/bookorbit_sync.bin", PositionFormat::Settled},
      {"/legacy-upgrade/.crosspoint/orbismart/0123456789abcdef0123456789abcdef.0", PositionFormat::Intent}};
  for (const auto& [path, format] : provenance) {
    ok = ok && worker.retain(path, format, source, status, id) == JournalResult::Ok &&
         status == PositionStatus::Supported;
  }
  pe.raw = "torn";
  ok = ok && ReadingPositions::select(&pe, &be, locator, sample) && sample.sourceOffset == 96;
  be.path = "/other/progress.bin.bak";
  ok = ok && !ReadingPositions::select(&pe, &be, locator, sample);
  be.path = "/legacy/progress.bin.bak";
  be.source.edition.bytes[0] = 8;
  ok = ok && !ReadingPositions::select(&pe, &be, locator, sample);
  ok = ok && journal.pending() == 13 && (before == 0 || before == 13);
  std::printf(
      "BookSync reading positions scenario %s: two-boot evidence, raw anchor, backup, unresolved; workspace=%zu\n",
      ok ? "PASS" : "FAIL", sizeof(worker));
  std::fflush(nullptr);
  std::_Exit(ok ? 0 : 1);
}
