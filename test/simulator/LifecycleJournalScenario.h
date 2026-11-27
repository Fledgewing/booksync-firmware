// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "booksync/storage/LifecycleJournal.h"

// Harness-only: real HAL filesystem, no transport and no reader/UI changes.
inline void runLifecycleJournalScenario() {
  const char* mode = std::getenv("BOOKSYNC_JOURNAL_SCENARIO");
  if (!mode) return;
  using namespace booksync::storage;
  constexpr PublishPaths paths{"/journal/state", "/journal/state.tmp", "/journal/state.bak"};
  LifecycleJournal journal(paths, {1400, 1});
  const auto recovered = journal.recover();
  bool ok = recovered == JournalResult::Ok;
  uint64_t id = 0;
  if (strcmp(mode, "blocked") == 0) {
    JournalEntry intent{0, "toy", "synthetic-account", "synthetic-document", "capture", "", 0};
    ok = recovered == JournalResult::RecoveryRequired && journal.enqueue(intent, id) == JournalResult::RecoveryRequired;
  } else if (ok && strcmp(mode, "enqueue") == 0) {
    JournalEntry intent{0, "toy", "synthetic-account", "synthetic-document", "capture", "pending", 0};
    ok = journal.enqueue(intent, id) == JournalResult::Ok && journal.pending() == 1;
  } else if (ok) {
    ok = journal.next("toy", "synthetic-account", 0, id) == JournalResult::Ok;
    char buffer[192];
    JournalEntry entry;
    ok = ok && journal.read(id, entry, buffer, sizeof(buffer)) == JournalResult::Ok && entry.payload == "capture";
    if (ok && strcmp(mode, "checkpoint") == 0)
      ok = journal.checkpoint(id, "remote-unknown") == JournalResult::Ok;
    else if (ok && strcmp(mode, "capacity") == 0) {
      JournalEntry intent{0, "toy", "synthetic-account", "other", "new-capture", "", 0};
      uint64_t rejected = 0;
      ok = entry.checkpoint == "remote-unknown" && entry.acknowledged == 0 &&
           journal.enqueue(intent, rejected) == JournalResult::Capacity && journal.pending() == 1;
    } else if (ok && strcmp(mode, "ack") == 0) {
      for (int phase = 0; ok && phase < 5; ++phase)
        ok = journal.acknowledge(id, static_cast<LifecyclePhase>(phase), "confirmed") == JournalResult::Ok;
    } else if (ok && strcmp(mode, "restart") == 0)
      ok = entry.acknowledged == LifecycleJournal::AllPhases && entry.checkpoint == "confirmed";
  }
  if (ok)
    LOG_INF("BSJ", "BookSync journal scenario PASS");
  else
    LOG_ERR("BSJ", "BookSync journal scenario FAIL");
  std::fflush(nullptr);
  std::_Exit(ok ? 0 : 1);
}
