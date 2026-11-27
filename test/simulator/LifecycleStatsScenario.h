// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "booksync/server/bookorbit/Auth.h"
#include "booksync/server/bookorbit/LifecycleStats.h"
#include "booksync/services/SyncOwner.h"
namespace booksync_stats_scenario {
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
class Evidence final : public StatsEvidence {
 public:
  StatsInput input;
  bool load(const JournalEntry&, StatsInput& out) override {
    out = input;
    return true;
  }
};
class Remote final : public StatsTransport {
 public:
  const char* mode = "run";
  std::array<uint8_t, 6250> bits{};
  static bool valid(FsFile& f, void*) { return f.fileSize() == 6250; }
  StatsReply upload(const SyncWriterToken&, const StatsQuery& q, bool reconcile) override {
    if (q.hash != Hash || q.bookFileId != 22) return {};
    if (strcmp(mode, "before") == 0) std::_Exit(86);
    if (strcmp(mode, "timeout-before") == 0) return {StatsResult::Uncertain};
    if (Storage.exists("/stats/remote")) {
      auto f = Storage.open("/stats/remote", O_RDONLY);
      bool ok = f && f.read(bits.data(), bits.size()) == int(bits.size());
      const bool closed = f.close();
      if (!ok || !closed) return {};
    }
    StatsReply reply{StatsResult::Ok, Hash};
    for (size_t i = 0; i < q.count; ++i) {
      const auto& e = q.events[i];
      if (e.page >= 50000 || e.totalPages != 100000 || e.durationSeconds != 10 ||
          e.startTime != 1800000000ULL + e.page * 10)
        return {};
      uint8_t& byte = bits[e.page / 8];
      uint8_t bit = 1U << (e.page % 8);
      if (byte & bit)
        ++reply.duplicates;
      else {
        byte |= bit;
        ++reply.accepted;
      }
      if (e.startTime > reply.watermark) reply.watermark = e.startTime;
    }
    FilePublish publish({"/stats/remote", "/stats/remote.tmp", "/stats/remote.bak"}, valid);
    if (!publish.begin(bits.size()) || !publish.write(bits.data(), bits.size()) || !publish.finish())
      return {StatsResult::Uncertain};
    if (strcmp(mode, "after") == 0) std::_Exit(86);
    if (strcmp(mode, "timeout-after") == 0) return {StatsResult::Uncertain};
    if (strcmp(mode, "reconcile") == 0 && !reconcile) return {};
    return reply;
  }
};
class Adapter final : public ServerAdapter {
 public:
  Login login;
  Auth auth{login};
  Account account{auth};
  Evidence evidence;
  Remote remote;
  std::array<StatsEvent, LifecycleStats::Batch> batch{};
  char output[LifecycleJournal::MaxCheckpoint]{};
  LifecycleStats stats{account, evidence, remote, batch.data(), batch.size(), output, sizeof(output)};
  Capabilities capabilities() const override { return {}; }
  Snapshot cachedSnapshot() const override { return {}; }
  CommandStatus deliver(const SyncWriterToken&, Intent) override { return CommandStatus::Unavailable; }
  PhaseOutcome syncPhase(const SyncWriterToken& t, const PhaseRequest& r) override { return stats.run(t, r); }
};
inline void put(char* p, uint64_t n, unsigned bytes) {
  for (unsigned i = 0; i < bytes; ++i) p[i] = char(n >> (8 * i));
}
inline void observe(SyncCheckpoint point, uint64_t, unsigned, void*) {
  const char* cut = std::getenv("BOOKSYNC_STATS_CUT");
  if (cut && unsigned(point) == unsigned(std::atoi(cut))) std::_Exit(86);
}
}  // namespace booksync_stats_scenario
inline void runLifecycleStatsScenario() {
  const char* mode = std::getenv("BOOKSYNC_STATS_SCENARIO");
  if (!mode) return;
  using namespace booksync_stats_scenario;
  static Adapter adapter;
  static PageJournal pages{"/stats/pages"};
  LifecycleJournal journal{{"/stats/state", "/stats/state.tmp", "/stats/state.bak"}};
  static char scratch[SyncOwner::ScratchBytes];
  bool ok = pages.recover() == JournalResult::Ok && journal.recover() == JournalResult::Ok;
  SourceIdentity source{};
  source.book.bytes[0] = 1;
  source.edition.bytes[0] = 2;
  source.revision.bytes[0] = 3;
  adapter.evidence.input = {{"adapter:one", "account:one"}, 7, source, &pages, 1, 1900000000, 0, pages.count(), false};
  if (strcmp(mode, "seed") == 0) {
    char cp[LifecycleMatch::HeaderBytes + 6]{};
    memcpy(cp, "BSM1", 4);
    memcpy(cp + 4, source.book.bytes.data(), 16);
    memcpy(cp + 20, source.edition.bytes.data(), 16);
    memcpy(cp + 36, source.revision.bytes.data(), 32);
    put(cp + 68, 11, 8);
    put(cp + 76, 22, 8);
    cp[84] = 1;
    memcpy(cp + 85, Hash.data(), 32);
    put(cp + 117, 6, 2);
    memcpy(cp + 119, "native", 6);
    uint64_t id = 0;
    ok = ok && journal.enqueue({0, "adapter:one", "account:one", "local:one", "capture", {cp, sizeof(cp)}, 1}, id) ==
                   JournalResult::Ok;
  } else {
    static CredentialCapture credential;
    char secret[] = "synthetic";
    ok = ok &&
         credential.capture(LoginKind::Account, "synthetic-origin", "synthetic-user", secret, sizeof(secret) - 1) &&
         adapter.account.signIn(credential, adapter.account.snapshot().ticket) == AuthError::None;
    adapter.remote.mode = mode;
    SyncOwner owner(journal, adapter, "adapter:one", "account:one", scratch, sizeof(scratch), observe);
    ok = ok && owner.activate() == SyncResult::Ready;
    if (strncmp(mode, "timeout-", 8) == 0) {
      ok = ok && owner.step() == SyncResult::Uncertain;
      JournalEntry e;
      ok = ok && journal.read(1, e, scratch, sizeof(scratch)) == JournalResult::Ok && e.acknowledged == 1 &&
           e.attempting == 2;
    } else {
      for (unsigned i = 0; ok && i < (pages.count() + 31) / 32 + 1; ++i) {
        JournalEntry e;
        ok = journal.read(1, e, scratch, sizeof(scratch)) == JournalResult::Ok;
        if (e.acknowledged == 3) break;
        ok = ok && owner.step() == SyncResult::Advanced;
      }
      JournalEntry e;
      ok = ok && journal.read(1, e, scratch, sizeof(scratch)) == JournalResult::Ok && e.acknowledged == 3 &&
           owner.step() == SyncResult::Unavailable;
      auto remote = Storage.open("/stats/remote", O_RDONLY);
      ok = ok && remote &&
           remote.read(adapter.remote.bits.data(), adapter.remote.bits.size()) == int(adapter.remote.bits.size());
      ok = remote.close() && ok;
      for (unsigned i = 0; ok && i < 50000; ++i)
        ok = bool(adapter.remote.bits[i / 8] & (1U << (i % 8))) == (i < pages.count());
    }
  }
  if (ok)
    LOG_INF("BSST", "BookSync stats scenario PASS");
  else
    LOG_ERR("BSST", "BookSync stats scenario FAIL");
  std::fflush(nullptr);
  std::_Exit(ok ? 0 : 1);
}
