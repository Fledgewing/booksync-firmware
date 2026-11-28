// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "booksync/reader/Capture.h"
#include "booksync/storage/SourceAuthority.h"

inline void runNativeEvidenceScenario() {
  const auto* mode = std::getenv("BOOKSYNC_NATIVE_EVIDENCE_SCENARIO");
  if (!mode) return;
  using namespace booksync;
  static constexpr storage::ScopeView scope{"synthetic-server", "synthetic-account"};
  static constexpr storage::Revision revision{{3}};
  static storage::SourceAuthority authority(
      {"/.booksync/source.id", "/.booksync/source.id.tmp", "/.booksync/source.id.bak"});
  static storage::LifecycleJournal journal(
      {"/.booksync/native.queue", "/.booksync/native.queue.tmp", "/.booksync/native.queue.bak"});
  static std::array<char, 4096> bytes;
  static storage::SourceIdentity identity;
  bool ok = Storage.exists("/.booksync") || Storage.mkdir("/.booksync");
  ok = journal.recover() == storage::JournalResult::Ok && ok;
  if (std::strcmp(mode, "capture") == 0) {
    ok = authority.issue(
             scope, revision,
             [](uint8_t* out, size_t count, void*) {
               // Deterministic synthetic test-only entropy, never used by production.
               for (size_t i = 0; i < count; ++i) out[i] = uint8_t(i + 1);
               return true;
             },
             nullptr, identity) &&
         ok;
    reader::Capture capture(journal);
    ok = capture.open("/Books/Synthetic.epub", 1, {identity, scope, "synthetic-opaque-proof"}) ==
             storage::JournalResult::Ok &&
         ok;
    reader::Sample sample;
    sample.observedAt = 200;
    sample.sourceOffset = 123;
    sample.hasSourceOffset = true;
    ok = capture.update(sample, true) == storage::JournalResult::Ok && ok;
  } else {
    ok = authority.load(scope, revision, identity) && ok;
    storage::SourceIdentity foreign;
    ok = !authority.load({scope.server, "other-account"}, revision, foreign) && ok;
    uint64_t id = 0;
    storage::JournalEntry entry;
    reader::OpenCapture opened;
    reader::Sample sample;
    bool closed = false;
    ok = journal.next("native", "local-unbound", 0, id) == storage::JournalResult::Ok && ok;
    ok = journal.read(id, entry, bytes.data(), bytes.size()) == storage::JournalResult::Ok && ok;
    ok = reader::Capture::decodeOpen(entry.payload, opened) && opened.bound && opened.binding.source == identity &&
         opened.binding.scope == scope && opened.binding.proof == "synthetic-opaque-proof" && ok;
    ok = reader::Capture::decode(entry.checkpoint, sample, closed) && closed && sample.nativeOpen == id &&
         sample.observedAt == 200 && sample.sourceOffset == 123 && entry.acknowledged == 0 && ok;
  }
  LOG_INF("BSC", "BookSync native evidence scenario %s; source=exact; account=isolated; network=none",
          ok ? "PASS" : "FAIL");
  std::fflush(nullptr);
  std::_Exit(ok ? 0 : 1);
}
