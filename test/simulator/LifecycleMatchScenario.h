// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "booksync/server/bookorbit/Auth.h"
#include "booksync/server/bookorbit/LifecycleMatch.h"
#include "booksync/services/SyncOwner.h"
namespace booksync_match_scenario {
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
class Evidence final : public MatchEvidence {
 public:
  SourceIdentity source{};
  BindingView row{};
  Evidence() {
    source.book.bytes[0] = 1;
    source.edition.bytes[0] = 2;
    source.revision.bytes[0] = 3;
    row = {{"adapter:one", "account:one"}, {}, "/book.epub", source};
  }
  bool load(const JournalEntry&, MatchInput& input) override {
    input = {row.scope, 7, source, row.path, Hash, "Synthetic title", "Synthetic author", 1234, &row, 1};
    return true;
  }
};
class Remote final : public MatchTransport {
 public:
  const char* mode = "run";
  MatchRow row{Hash, 11, 22, true};
  static bool valid(FsFile& file, void*) {
    char byte = 0;
    return file.fileSize() == 1 && file.read(&byte, 1) == 1 && byte == '1';
  }
  MatchReply check(const SyncWriterToken&, const MatchQuery& q, bool reconcile) override {
    if (q.hash != Hash || q.source != "current_file") return {};
    if (std::strcmp(mode, "before") == 0) std::_Exit(86);
    if (std::strcmp(mode, "timeout-before") == 0) return {MatchResult::Uncertain};
    // A fake server persisted separately from the firmware journal. A repeated
    // match-check finds the existing association instead of adding another one.
    if (Storage.exists("/match/remote")) {
      auto file = Storage.open("/match/remote", O_RDONLY);
      bool ok = file && valid(file, nullptr);
      const bool closed = file.close();
      if (!ok || !closed) return {};
    } else {
      FilePublish publish({"/match/remote", "/match/remote.tmp", "/match/remote.bak"}, valid);
      const char byte = '1';
      if (!publish.begin(1) || !publish.write(&byte, 1) || !publish.finish()) return {MatchResult::Uncertain};
    }
    if (std::strcmp(mode, "after") == 0) std::_Exit(86);
    if (std::strcmp(mode, "timeout-after") == 0) return {MatchResult::Uncertain};
    if (std::strcmp(mode, "reconcile") == 0 && !reconcile) return {};
    return {MatchResult::Ok, &row, 1};
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
  LifecycleMatch match{account, evidence, remote, output, sizeof(output)};
  Capabilities capabilities() const override { return {}; }
  Snapshot cachedSnapshot() const override { return {}; }
  CommandStatus deliver(const SyncWriterToken&, Intent) override { return CommandStatus::Unavailable; }
  PhaseOutcome syncPhase(const SyncWriterToken& t, const PhaseRequest& r) override { return match.run(t, r); }
};
inline void observe(SyncCheckpoint point, uint64_t, unsigned, void*) {
  const char* cut = std::getenv("BOOKSYNC_MATCH_CUT");
  if (cut && unsigned(point) == unsigned(std::atoi(cut))) std::_Exit(86);
}
}  // namespace booksync_match_scenario
inline void runLifecycleMatchScenario() {
  const char* mode = std::getenv("BOOKSYNC_MATCH_SCENARIO");
  if (!mode) return;
  using namespace booksync_match_scenario;
  static Adapter adapter;
  static CredentialCapture input;
  char secret[] = "synthetic";
  bool ok = input.capture(LoginKind::Account, "synthetic-origin", "synthetic-user", secret, sizeof(secret) - 1) &&
            adapter.account.signIn(input, adapter.account.snapshot().ticket) == AuthError::None;
  adapter.remote.mode = mode;
  LifecycleJournal journal({"/match/state", "/match/state.tmp", "/match/state.bak"});
  static char scratch[SyncOwner::ScratchBytes];
  if (std::strcmp(mode, "seed") == 0) {
    uint64_t id = 0;
    ok = ok && journal.recover() == JournalResult::Ok &&
         journal.enqueue({0, "adapter:one", "account:one", "local:one", "capture", "native", 0}, id) ==
             JournalResult::Ok;
  } else {
    SyncOwner owner(journal, adapter, "adapter:one", "account:one", scratch, sizeof(scratch), observe);
    ok = ok && owner.activate() == SyncResult::Ready;
    const auto result = ok ? owner.step() : SyncResult::Invalid;
    JournalEntry entry;
    ok = ok && journal.read(1, entry, scratch, sizeof(scratch)) == JournalResult::Ok;
    if (std::strncmp(mode, "timeout-", 8) == 0) {
      ok = ok && result == SyncResult::Uncertain && entry.acknowledged == 0 && entry.attempting == 1 &&
           entry.checkpoint == "native";
    } else {
      MatchCheckpoint checkpoint;
      ok = ok && (result == SyncResult::Advanced || result == SyncResult::Unavailable) && entry.acknowledged == 1 &&
           (entry.attempting == (result == SyncResult::Advanced ? 0 : 2)) &&
           LifecycleMatch::decode(entry.checkpoint, checkpoint) && checkpoint.source == adapter.evidence.source &&
           checkpoint.hash == Hash && checkpoint.bookId == 11 && checkpoint.bookFileId == 22 &&
           checkpoint.native == "native" && owner.step() == SyncResult::Unavailable && journal.pending() == 1;
    }
  }
  if (ok)
    LOG_INF("BSM", "BookSync match scenario PASS");
  else
    LOG_ERR("BSM", "BookSync match scenario FAIL");
  std::fflush(nullptr);
  std::_Exit(ok ? 0 : 1);
}
