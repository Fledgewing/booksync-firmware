// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "booksync/services/Finish.h"
#include "booksync/services/SyncOwner.h"
namespace booksync_finish_scenario {
using namespace booksync;
using namespace booksync::storage;
class Proof final : public FinishPositionProof {
 public:
  bool delivered(const JournalEntry& e) const override { return e.payload == "native"; }
};
class Toy final : public ServerAdapter {
 public:
  bool uncertain = false;
  Capabilities capabilities() const override { return {}; }
  Snapshot cachedSnapshot() const override { return {}; }
  CommandStatus deliver(const SyncWriterToken&, Intent) override { return CommandStatus::Unavailable; }
  static bool valid(FsFile& f, void*) { return f.fileSize() == 11; }
  static bool save(const char* path, const char* tmp, const char* bak, const char* bytes) {
    FilePublish p({path, tmp, bak}, valid);
    return p.begin(11) && p.write(bytes, 11) && p.finish();
  }
  static bool load(char* bytes) {
    if (!Storage.exists("/finish/remote")) return true;
    auto f = Storage.open("/finish/remote", O_RDONLY);
    const bool ok = f && f.read(bytes, 11) == 11;
    const bool closed = f.close();
    return ok && closed;
  }
  PhaseOutcome syncPhase(const SyncWriterToken&, const PhaseRequest& r) override {
    if (r.phase == LifecyclePhase::Progress) {
      char bytes[11]{};
      if (!load(bytes)) return PhaseOutcome::Unavailable;
      bytes[0] = 1;
      if (!save("/finish/remote", "/finish/remote.tmp", "/finish/remote.bak", bytes)) return PhaseOutcome::Uncertain;
    }
    return PhaseOutcome::Confirmed;
  }
  PhaseOutcome finishPhase(const SyncWriterToken&, const FinishRequest& r) override {
    char bytes[11]{};
    if (!load(bytes) || !bytes[0] || r.entry.checkpoint != "resolved-identity" || r.entry.id > 3)
      return PhaseOutcome::Unavailable;
    const bool undo = r.intent.undoOf != 0;
    if (r.intent.desired.status != (undo ? ReadingStatus::Reading : ReadingStatus::Finished) ||
        !r.intent.desired.ratingKnown || r.intent.desired.rating != (undo ? 2 : 4) ||
        r.intent.before.status != (undo ? ReadingStatus::Finished : ReadingStatus::Reading) ||
        !r.intent.before.ratingKnown || r.intent.before.rating != (undo ? 4 : 2))
      return PhaseOutcome::Unavailable;
    const auto phase = unsigned(r.phase);
    const size_t slot = 1 + (r.entry.id - 2) * 5 + phase;
    if (phase == 2 && !r.intent.day.empty() && !bytes[slot - 1]) return PhaseOutcome::Unavailable;
    if (phase == 3 && r.intent.desired.status != ReadingStatus::Unknown && !bytes[slot - 1])
      return PhaseOutcome::Unavailable;
    if (phase == 4 && r.intent.desired.ratingKnown && !bytes[slot - 1]) return PhaseOutcome::Unavailable;
    const char* cut = std::getenv("BOOKSYNC_FINISH_CUT");
    char label[32];
    std::snprintf(label, sizeof(label), "before:%u", phase);
    if (cut && std::strcmp(cut, label) == 0) std::_Exit(86);
    if (!bytes[slot]) {
      bytes[slot] = 1;
      if (!save("/finish/remote", "/finish/remote.tmp", "/finish/remote.bak", bytes)) return PhaseOutcome::Uncertain;
    }
    std::snprintf(label, sizeof(label), "after:%u", phase);
    if (cut && std::strcmp(cut, label) == 0) std::_Exit(86);
    return uncertain ? PhaseOutcome::Uncertain : PhaseOutcome::Confirmed;
  }
};
}  // namespace booksync_finish_scenario
inline void runFinishScenario() {
  const char* mode = std::getenv("BOOKSYNC_FINISH_SCENARIO");
  if (!mode) return;
  using namespace booksync_finish_scenario;
  LifecycleJournal journal({"/finish/journal", "/finish/journal.tmp", "/finish/journal.bak"});
  static char scratch[SyncOwner::ScratchBytes], workspace[SyncOwner::ScratchBytes];
  Proof proof;
  Finish finish(journal, proof, workspace, sizeof(workspace));
  Toy toy;
  bool ok = journal.recover() == JournalResult::Ok;
  if (std::strcmp(mode, "seed") == 0) {
    uint64_t position = 0, id = 0;
    ok = ok && journal.enqueue({0, "toy:grove", "scope:fern", "leaf:violet", "native", "resolved-identity"},
                               position) == JournalResult::Ok;
    ok = ok &&
         finish.enqueue(position, "toy:grove", "scope:fern", "leaf:violet", {ReadingStatus::Reading, true, 2},
                        {ReadingStatus::Finished, true, 4}, "2026-11-27", id) == JournalResult::Ok &&
         id == 2;
  } else {
    if (std::strcmp(mode, "cancel") == 0 || std::strcmp(mode, "undo") == 0) {
      uint64_t id = 0;
      ok = ok && finish.undo(2, "toy:grove", "scope:fern", id) == JournalResult::Ok;
      uint64_t again = 0;
      ok = ok && finish.undo(2, "toy:grove", "scope:fern", again) == JournalResult::Ok && again == id;
    }
    SyncOwner owner(journal, toy, "toy:grove", "scope:fern", scratch, sizeof(scratch), nullptr, nullptr, &finish);
    ok = ok && owner.activate() == SyncResult::Ready;
    toy.uncertain = std::strcmp(mode, "uncertain") == 0;
    for (unsigned i = 0; ok && i < 30; ++i) {
      const auto result = owner.step();
      if (result == SyncResult::Uncertain) {
        ok = toy.uncertain;
        break;
      }
      if (result == SyncResult::Idle) break;
      ok = result == SyncResult::Advanced || result == SyncResult::Completed;
    }
  }
  if (ok)
    LOG_INF("BSF", "BookSync finish scenario PASS");
  else
    LOG_ERR("BSF", "BookSync finish scenario FAIL");
  std::fflush(nullptr);
  std::_Exit(ok ? 0 : 1);
}
