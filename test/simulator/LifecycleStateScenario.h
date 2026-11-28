// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "booksync/server/bookorbit/Auth.h"
#include "booksync/server/bookorbit/LifecycleMatch.h"
#include "booksync/server/bookorbit/LifecycleState.h"
#include "booksync/services/SyncOwner.h"
namespace booksync_state_scenario {
using namespace booksync;
using namespace booksync::storage;
using namespace booksync::server;
constexpr std::string_view Hash = "0123456789abcdef0123456789abcdef";
class Login final : public AuthTransport {
 public:
  AuthCapabilities capabilities() const override { return {true, true, true}; }
  AuthReply authenticate(LoginKind, std::string_view, std::string_view, std::string_view, bool) override {
    return {200, false, 7, 7, 9};
  }
  void finishAuth(bool) override {}
  void clearAuth() override {}
};
class Evidence final : public StateEvidence {
 public:
  SourceIdentity source{};
  const char* mode = "run";
  Evidence() {
    source.book.bytes[0] = 1;
    source.edition.bytes[0] = 2;
    source.revision.bytes[0] = 3;
  }
  bool load(const JournalEntry&, StateInput& input) override {
    input.scope = {"adapter:one", "account:one"};
    input.syncOwner = 7;
    input.source = source;
    input.status = "reading";
    input.open = std::strcmp(mode, "open") == 0;
    return true;
  }
  static bool valid(FsFile& file, void*) {
    char bytes[2]{};
    return file.fileSize() == 2 && file.read(bytes, 2) == 2 && bytes[0] == '4' && bytes[1] == '1';
  }
  bool apply(const JournalEntry&, const StateInput& input, const StateValues& value) override {
    if (input.open || !value.statusKnown || value.status != "complete" || !value.ratingKnown || value.rating != 4)
      return false;
    FilePublish publish({"/state/native", "/state/native.tmp", "/state/native.bak"}, valid);
    const char bytes[] = "41";
    if (!publish.begin(2) || !publish.write(bytes, 2) || !publish.finish()) return false;
    if (std::strcmp(mode, "apply-cut") == 0) std::_Exit(86);
    return true;
  }
};
class Remote final : public StateTransport {
 public:
  const char* mode = "run";
  StateValues values{true, "complete", "2026-11-27", true, 4, true, false, {}};
  static bool valid(FsFile& file, void*) {
    char byte = 0;
    return file.fileSize() == 1 && file.read(&byte, 1) == 1 && byte == '1';
  }
  StateReply exchange(const SyncWriterToken&, const StateQuery& q, bool reconcile) override {
    if (q.hash != Hash || q.status != "reading") return {};
    if (std::strcmp(mode, "before") == 0) std::_Exit(86);
    if (std::strcmp(mode, "timeout-before") == 0) return {StateResult::Uncertain, {}};
    // A fake server persisted separately from the firmware journal. A repeated
    // match-check finds the existing association instead of adding another one.
    if (Storage.exists("/state/remote")) {
      auto file = Storage.open("/state/remote", O_RDONLY);
      bool ok = file && valid(file, nullptr);
      const bool closed = file.close();
      if (!ok || !closed) return {};
    } else {
      FilePublish publish({"/state/remote", "/state/remote.tmp", "/state/remote.bak"}, valid);
      const char byte = '1';
      if (!publish.begin(1) || !publish.write(&byte, 1) || !publish.finish()) return {StateResult::Uncertain, {}};
    }
    if (std::strcmp(mode, "after") == 0) std::_Exit(86);
    if (std::strcmp(mode, "timeout-after") == 0) return {StateResult::Uncertain, {}};
    if (std::strcmp(mode, "reconcile") == 0 && !reconcile) return {};
    return {StateResult::Ok, values};
  }
};
class Adapter final : public ServerAdapter {
 public:
  Login login;
  Auth auth{login};
  Account account{auth};
  Evidence evidence;
  Remote remote;
  char output[LifecycleJournal::MaxCheckpoint]{};
  LifecycleState state{account, evidence, remote, output, sizeof(output)};
  Capabilities capabilities() const override { return {}; }
  Snapshot cachedSnapshot() const override { return {}; }
  CommandStatus deliver(const SyncWriterToken&, Intent) override { return CommandStatus::Unavailable; }
  PhaseOutcome syncPhase(const SyncWriterToken& t, const PhaseRequest& r) override { return state.run(t, r); }
};
inline void observe(SyncCheckpoint point, uint64_t, unsigned, void*) {
  const char* cut = std::getenv("BOOKSYNC_STATE_CUT");
  if (cut && unsigned(point) == unsigned(std::atoi(cut))) std::_Exit(86);
}
}  // namespace booksync_state_scenario
inline void runLifecycleStateScenario() {
  const char* mode = std::getenv("BOOKSYNC_STATE_SCENARIO");
  if (!mode) return;
  using namespace booksync_state_scenario;
  static Adapter adapter;
  static CredentialCapture input;
  char secret[] = "synthetic";
  bool ok = input.capture(LoginKind::Account, "synthetic-origin", "synthetic-user", secret, sizeof(secret) - 1) &&
            adapter.account.signIn(input, adapter.account.snapshot().ticket) == AuthError::None;
  adapter.remote.mode = mode;
  adapter.evidence.mode = mode;
  LifecycleJournal journal({"/state/state", "/state/state.tmp", "/state/state.bak"});
  static char scratch[SyncOwner::ScratchBytes];
  if (std::strcmp(mode, "seed") == 0) {
    uint64_t id = 0;
    ok = ok && journal.recover() == JournalResult::Ok &&
         journal.enqueue({0, "adapter:one", "account:one", "local:one", "capture", "native", 0}, id) ==
             JournalResult::Ok;
    static char matched[LifecycleMatch::HeaderBytes]{};
    std::memcpy(matched, "BSM1", 4);
    matched[4] = 1;
    matched[20] = 2;
    matched[36] = 3;
    matched[68] = 11;
    matched[76] = 22;
    matched[84] = 1;
    std::memcpy(matched + 85, Hash.data(), 32);
    for (unsigned phase = 0; ok && phase < 3; ++phase) {
      ok = journal.attempt(1, static_cast<LifecyclePhase>(phase)) == JournalResult::Ok &&
           journal.acknowledge(1, static_cast<LifecyclePhase>(phase), {matched, sizeof(matched)}) == JournalResult::Ok;
    }
  } else {
    SyncOwner owner(journal, adapter, "adapter:one", "account:one", scratch, sizeof(scratch), observe);
    ok = ok && owner.activate() == SyncResult::Ready;
    auto result = ok ? owner.step() : SyncResult::Invalid;
    JournalEntry entry;
    ok = ok && journal.read(1, entry, scratch, sizeof(scratch)) == JournalResult::Ok;
    if (std::strncmp(mode, "timeout-", 8) == 0) {
      ok = ok && result == SyncResult::Uncertain && entry.acknowledged == 7 && entry.attempting == 4;
    } else if (std::strcmp(mode, "open") == 0) {
      ok = ok && result == SyncResult::Advanced && owner.step() == SyncResult::Unavailable &&
           !Storage.exists("/state/native");
    } else {
      if (entry.acknowledged == 7 && result == SyncResult::Advanced) result = owner.step();
      ok = ok && journal.read(1, entry, scratch, sizeof(scratch)) == JournalResult::Ok;
      MatchCheckpoint checkpoint;
      ok = ok && (result == SyncResult::Advanced || result == SyncResult::Unavailable) && entry.acknowledged == 15 &&
           LifecycleMatch::decode(entry.checkpoint, checkpoint) && checkpoint.source == adapter.evidence.source &&
           checkpoint.hash == Hash && checkpoint.bookId == 11 && checkpoint.bookFileId == 22 &&
           Storage.exists("/state/native") && owner.step() == SyncResult::Unavailable && journal.pending() == 1;
    }
  }
  if (ok)
    LOG_INF("BSM", "BookSync state scenario PASS");
  else
    LOG_ERR("BSM", "BookSync state scenario FAIL");
  std::fflush(nullptr);
  std::_Exit(ok ? 0 : 1);
}
