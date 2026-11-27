// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "booksync/services/SyncOwner.h"

namespace booksync_sync_scenario {
using namespace booksync;
using namespace booksync::storage;
class Toy final : public ServerAdapter {
 public:
  static bool validRemote(FsFile& file, void*) {
    char bits[5];
    if (file.fileSize() != sizeof(bits) || file.read(bits, sizeof(bits)) != sizeof(bits)) return false;
    for (char bit : bits)
      if (bit != 0 && bit != 1) return false;
    return true;
  }
  bool uncertain = false;
  Capabilities capabilities() const override { return {}; }
  Snapshot cachedSnapshot() const override { return {}; }
  CommandStatus deliver(const SyncWriterToken&, Intent) override { return CommandStatus::Unavailable; }
  PhaseOutcome syncPhase(const SyncWriterToken&, const PhaseRequest& r) override {
    if (r.entry.adapter != "toy:grove" || r.entry.owner != "scope:fern" || r.entry.id != 1 ||
        r.entry.document != "leaf:violet" || r.entry.payload != "offline" || r.entry.checkpoint != "native-capture")
      return PhaseOutcome::Unavailable;
    // Synthetic durable remote ledger. One scoped event; nonnumeric keys. A
    // duplicate/reconcile reads the same phase, never adds another side effect.
    char bits[5]{};
    if (Storage.exists("/sync/remote")) {
      auto file = Storage.open("/sync/remote", O_RDONLY);
      const bool ok = file && file.read(bits, sizeof(bits)) == sizeof(bits);
      const bool closed = file.close();
      if (!ok || !closed) return PhaseOutcome::Unavailable;
    }
    if (!bits[unsigned(r.phase)]) {
      bits[unsigned(r.phase)] = 1;
      FilePublish writer({"/sync/remote", "/sync/remote.tmp", "/sync/remote.bak"}, validRemote);
      if (!writer.begin(sizeof(bits)) || !writer.write(bits, sizeof(bits)) || !writer.finish())
        return PhaseOutcome::Uncertain;
    }
    return uncertain ? PhaseOutcome::Uncertain : PhaseOutcome::Confirmed;
  }
};
inline void observe(SyncCheckpoint point, uint64_t, unsigned phase, void*) {
  const char* cut = std::getenv("BOOKSYNC_SYNC_CUT");
  char label[32];
  snprintf(label, sizeof(label), "%u:%u", unsigned(point), phase);
  if (cut && strcmp(cut, label) == 0) {
    std::fflush(nullptr);
    std::_Exit(86);  // Power loss: no owner destructor or graceful teardown.
  }
}
}  // namespace booksync_sync_scenario
inline void runSyncOwnerScenario() {
  const char* mode = std::getenv("BOOKSYNC_SYNC_SCENARIO");
  if (!mode) return;
  using namespace booksync_sync_scenario;
  constexpr PublishPaths paths{"/sync/state", "/sync/state.tmp", "/sync/state.bak"};
  LifecycleJournal journal(paths);
  Toy toy;
  // Harness-owned scratch; production owner never allocates or uses a large stack.
  static char scratch[SyncOwner::ScratchBytes];
  bool ok = true;
  if (strcmp(mode, "seed") == 0) {
    uint64_t id = 0;
    ok = journal.recover() == JournalResult::Ok &&
         journal.enqueue({0, "toy:grove", "scope:fern", "leaf:violet", "offline", "native-capture", 0}, id) ==
             JournalResult::Ok;
  } else {
    SyncOwner owner(journal, toy, "toy:grove", "scope:fern", scratch, sizeof(scratch), observe);
    ok = owner.activate() == SyncResult::Ready;
    SyncOwner second(journal, toy, "toy:grove", "scope:fern", scratch, sizeof(scratch));
    ok = ok && second.activate() == SyncResult::Busy && second.step() == SyncResult::Inactive;
    if (strcmp(mode, "uncertain") == 0) {
      toy.uncertain = true;
      ok = ok && owner.step() == SyncResult::Uncertain && journal.pending() == 1;
    } else if (strcmp(mode, "transition") == 0) {
      ok = ok && owner.deactivate() == SyncResult::Ready;
      SyncOwner other(journal, toy, "toy:grove", "scope:other", scratch, sizeof(scratch));
      ok = ok && other.activate() == SyncResult::Ready && other.step() == SyncResult::Idle && journal.pending() == 1;
    } else {
      for (unsigned i = 0; ok && i < 7; ++i) {
        const auto result = owner.step();
        ok = result == SyncResult::Advanced || result == SyncResult::Completed || result == SyncResult::Idle;
        if (result == SyncResult::Idle) break;
      }
      ok = ok && journal.pending() == 0;
    }
  }
  if (ok)
    LOG_INF("BSS", "BookSync sync owner scenario PASS");
  else
    LOG_ERR("BSS", "BookSync sync owner scenario FAIL");
  std::fflush(nullptr);
  std::_Exit(ok ? 0 : 1);
}
