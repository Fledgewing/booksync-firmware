// SPDX-License-Identifier: GPL-3.0-or-later
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <array>
#include <map>
#include <tuple>

#include "booksync/reader/PageCapture.h"
#include "booksync/server/bookorbit/Auth.h"
#include "booksync/server/bookorbit/LifecycleStats.h"
#include "booksync/services/SyncOwner.h"
using namespace booksync;
using namespace booksync::server;
using namespace booksync::storage;
namespace {
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
  Account* revoke = nullptr;
  bool load(const JournalEntry&, StatsInput& out) override {
    out = input;
    if (revoke) revoke->signOut(revoke->snapshot().ticket);
    return true;
  }
};
class Remote final : public StatsTransport {
 public:
  std::map<std::tuple<uint64_t, uint32_t>, uint32_t> rows;
  unsigned calls = 0, reconciles = 0;
  bool before = false, timeout = false, cut = false, malformed = false, unmatched = false;
  Account* revoke = nullptr;
  StatsReply upload(const SyncWriterToken&, const StatsQuery& query, bool reconcile) override {
    ++calls;
    reconciles += reconcile;
    EXPECT_EQ(query.hash, Hash);
    EXPECT_EQ(query.bookFileId, 22);
    if (before) {
      if (cut) throw PowerCut{};
      return {StatsResult::Uncertain};
    }
    StatsReply reply{StatsResult::Ok, Hash};
    for (size_t i = 0; i < query.count; ++i) {
      const auto& e = query.events[i];
      EXPECT_LE(e.durationSeconds, 86400);
      EXPECT_EQ(e.totalPages, 10000);
      if (rows.emplace(std::make_tuple(e.startTime, e.page), e.durationSeconds).second)
        ++reply.accepted;
      else
        ++reply.duplicates;
      reply.watermark = std::max(reply.watermark, e.startTime);
    }
    if (revoke) revoke->signOut(revoke->snapshot().ticket);
    if (cut) throw PowerCut{};
    if (timeout) return {StatsResult::Uncertain};
    if (malformed) ++reply.duplicates;
    reply.unmatched = unmatched;
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
  std::array<char, LifecycleJournal::MaxCheckpoint> output{};
  std::array<StatsEvent, LifecycleStats::Batch> batch{};
  LifecycleStats stats{account, evidence, remote, batch.data(), batch.size(), output.data(), output.size()};
  Capabilities capabilities() const override { return {}; }
  Snapshot cachedSnapshot() const override { return {}; }
  CommandStatus deliver(const SyncWriterToken&, Intent) override { return CommandStatus::Unavailable; }
  PhaseOutcome syncPhase(const SyncWriterToken& t, const PhaseRequest& r) override { return stats.run(t, r); }
  void signIn(LoginKind kind = LoginKind::Account) {
    CredentialCapture c;
    char secret[] = "synthetic";
    ASSERT_TRUE(c.capture(kind, "synthetic-origin", "synthetic-user", secret, sizeof(secret) - 1));
    ASSERT_EQ(account.signIn(c, account.snapshot().ticket), AuthError::None);
  }
};
void put(char* p, uint64_t n, unsigned bytes) {
  for (unsigned i = 0; i < bytes; ++i) p[i] = char(n >> (8 * i));
}
struct LifecycleStatsTest : testing::Test {
  PageJournal history{"/pages"};
  LifecycleJournal journal{{"/queue", "/queue.tmp", "/queue.bak"}};
  Adapter adapter;
  std::array<char, SyncOwner::ScratchBytes> scratch{};
  SourceIdentity source{};
  void SetUp() override {
    card = {};
    source.book.bytes[0] = 1;
    source.edition.bytes[0] = 2;
    source.revision.bytes[0] = 3;
    ASSERT_EQ(history.recover(), JournalResult::Ok);
    for (unsigned i = 0; i < 70; ++i)
      ASSERT_EQ(history.append({1, 1800000000ULL + i / 2, i + 1, 90000, 2, i, 100, 0, i, 10000, 0}), JournalResult::Ok);
    adapter.evidence.input = {{"adapter:one", "account:one"}, 7, source, &history, 1, 1800000100, 0, 70, false};
    char checkpoint[LifecycleMatch::HeaderBytes + 6]{};
    memcpy(checkpoint, "BSM1", 4);
    memcpy(checkpoint + 4, source.book.bytes.data(), 16);
    memcpy(checkpoint + 20, source.edition.bytes.data(), 16);
    memcpy(checkpoint + 36, source.revision.bytes.data(), 32);
    put(checkpoint + 68, 11, 8);
    put(checkpoint + 76, 22, 8);
    checkpoint[84] = 1;
    memcpy(checkpoint + 85, Hash.data(), 32);
    put(checkpoint + 117, 6, 2);
    memcpy(checkpoint + 119, "native", 6);
    ASSERT_EQ(journal.recover(), JournalResult::Ok);
    uint64_t id = 0;
    ASSERT_EQ(journal.enqueue(
                  {0, "adapter:one", "account:one", "local:one", "capture", {checkpoint, sizeof(checkpoint)}, 1}, id),
              JournalResult::Ok);
    adapter.signIn();
    card.events.clear();
  }
  SyncResult step() {
    SyncOwner owner(journal, adapter, "adapter:one", "account:one", scratch.data(), scratch.size());
    EXPECT_EQ(owner.activate(), SyncResult::Ready);
    return owner.step();
  }
  JournalEntry read() {
    JournalEntry e;
    EXPECT_EQ(journal.read(1, e, scratch.data(), scratch.size()), JournalResult::Ok);
    return e;
  }
  void complete() {
    for (unsigned i = 0; i < 3; ++i) ASSERT_EQ(step(), SyncResult::Advanced);
    EXPECT_EQ(read().acknowledged, 3);
  }
};
}  // namespace
TEST_F(LifecycleStatsTest, BatchesPreserveNativeMatchAndEqualTimestampRowsWithoutDoubleCount) {
  ASSERT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(read().acknowledged, 1);
  EXPECT_EQ(read().attempting, 2);
  card.restart();
  ASSERT_EQ(history.recover(), JournalResult::Ok);
  ASSERT_EQ(step(), SyncResult::Advanced);
  ASSERT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(adapter.remote.rows.size(), 70);
  EXPECT_EQ(read().acknowledged, 3);
  std::string_view matched;
  uint32_t cursor, begin, end;
  uint64_t open, captured;
  ASSERT_TRUE(LifecycleStats::decode(read().checkpoint, matched, cursor, begin, end, open, captured));
  EXPECT_EQ(cursor, 70);
  MatchCheckpoint saved;
  ASSERT_TRUE(LifecycleMatch::decode(matched, saved));
  EXPECT_EQ(saved.native, "native");
  ASSERT_TRUE(LifecycleMatch::decode(read().checkpoint, saved));
  EXPECT_EQ(saved.native, "native");
  EXPECT_EQ(saved.source, source);
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(adapter.remote.calls, 3);
}
TEST_F(LifecycleStatsTest, TimeoutBeforeAndAfterCommitReconcileExactFrozenRows) {
  const auto seed = card.disk;
  for (bool before : {true, false}) {
    card.disk = seed;
    card.restart();
    adapter.remote.rows.clear();
    adapter.remote.before = before;
    adapter.remote.timeout = true;
    ASSERT_EQ(step(), SyncResult::Uncertain);
    EXPECT_EQ(read().acknowledged, 1);
    card.restart();
    adapter.remote.before = false;
    adapter.remote.timeout = false;
    complete();
    EXPECT_EQ(adapter.remote.rows.size(), 70);
  }
}
TEST_F(LifecycleStatsTest, CrashOnBothSidesOfCommitRestartsAndDeduplicates) {
  const auto seed = card.disk;
  for (bool before : {true, false}) {
    card.disk = seed;
    card.restart();
    adapter.remote.rows.clear();
    adapter.remote.before = before;
    adapter.remote.cut = true;
    EXPECT_THROW(step(), PowerCut);
    card.restart();
    adapter.remote.before = false;
    adapter.remote.cut = false;
    complete();
    EXPECT_EQ(adapter.remote.rows.size(), 70);
  }
}
TEST_F(LifecycleStatsTest, EveryLocalCheckpointCutRetainsRemoteDedupAndPhaseOrder) {
  const auto seed = card.disk;
  size_t operations = 0;
  {
    SyncOwner owner(journal, adapter, "adapter:one", "account:one", scratch.data(), scratch.size());
    ASSERT_EQ(owner.activate(), SyncResult::Ready);
    card.events.clear();
    ASSERT_EQ(owner.step(), SyncResult::Advanced);
    operations = card.events.size();
  }
  for (bool eager : {false, true})
    for (size_t cut = 0; cut < operations; ++cut) {
      SCOPED_TRACE(cut);
      card.disk = seed;
      card.restart();
      card.eager = eager;
      adapter.remote.rows.clear();
      {
        SyncOwner owner(journal, adapter, "adapter:one", "account:one", scratch.data(), scratch.size());
        ASSERT_EQ(owner.activate(), SyncResult::Ready);
        card.events.clear();
        card.stop = cut;
        EXPECT_THROW(owner.step(), PowerCut);
      }
      card.restart();
      ASSERT_EQ(journal.recover(), JournalResult::Ok);
      for (unsigned attempts = 0; read().acknowledged == 1 && attempts < 3; ++attempts)
        ASSERT_EQ(step(), SyncResult::Advanced);
      EXPECT_EQ(read().acknowledged, 3);
      EXPECT_EQ(adapter.remote.rows.size(), 70);
    }
}
TEST_F(LifecycleStatsTest, AccountEpochAndScopeRevisionMismatchNeverAcknowledgeWrongOwner) {
  adapter.evidence.input.scope.account = "other";
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(adapter.remote.calls, 0);
  adapter.evidence.input.scope.account = "account:one";
  adapter.evidence.input.source.revision.bytes[0] = 9;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(adapter.remote.calls, 0);
  adapter.evidence.input.source = source;
  adapter.remote.revoke = &adapter.account;
  EXPECT_EQ(step(), SyncResult::Uncertain);
  EXPECT_EQ(read().acknowledged, 1);
  adapter.remote.revoke = nullptr;
  adapter.signIn();
  complete();
  EXPECT_EQ(adapter.remote.rows.size(), 70);
}
TEST_F(LifecycleStatsTest, MalformedUnmatchedAndInsufficientCapacityRetainPendingData) {
  adapter.remote.malformed = true;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(read().acknowledged, 1);
  adapter.remote.malformed = false;
  adapter.remote.unmatched = true;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(read().acknowledged, 1);
}
TEST_F(LifecycleStatsTest, SnapshotCannotGrowBetweenPartialCheckpoints) {
  ASSERT_EQ(step(), SyncResult::Advanced);
  adapter.evidence.input.end = 69;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(read().acknowledged, 1);
}
TEST_F(LifecycleStatsTest, MissingStatsAndAmbiguousMetadataDoNotFabricateTime) {
  adapter.evidence.input.metadataAmbiguous = true;
  ASSERT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(read().acknowledged, 3);
  EXPECT_EQ(adapter.remote.calls, 0);
}
TEST(PageCaptureTest, QueueIsMemoryOnlyAndOverflowPersistsAnExplicitGap) {
  card = {};
  PageJournal journal{"/pages"};
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  booksync::reader::PageCapture capture;
  capture.start({1, 1800000000, 0, 0, 0, 1, 100, 0, 0, 0, 0});
  EXPECT_TRUE(capture.needsLocation(0, 1, 100));
  capture.locate(0, 1, 100, 1, 10000);
  EXPECT_FALSE(capture.needsLocation(0, 1, 100));
  card.events.clear();
  for (unsigned i = 0; i < 12; ++i) {
    capture.start({1, 1800000000 + i, 0, 0, 0, i, 100, 0, i, 10000, 0});
    capture.accept(1800000010 + i, 10);
  }
  EXPECT_TRUE(card.events.empty());
  ASSERT_EQ(capture.flush(journal), JournalResult::Ok);
  EXPECT_EQ(journal.count(), 9);
  PageEvent e;
  ASSERT_EQ(journal.read(8, e), JournalResult::Ok);
  EXPECT_EQ(e.flags, PageEvent::Gap);
}

TEST_F(LifecycleStatsTest, GapForThisSourceBlocksAndOtherSourcesDoNotInventEvents) {
  ASSERT_EQ(history.append({1, 1800000000, 0, 0, 0, 0, 0, 0, 0, 0, PageEvent::Gap}), JournalResult::Ok);
  adapter.evidence.input.begin = 70;
  adapter.evidence.input.end = 71;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(adapter.remote.calls, 0);
  EXPECT_EQ(read().acknowledged, 1);
}
TEST_F(LifecycleStatsTest, AcquisitionOnlyAndEvidenceEpochChangeCannotSend) {
  ASSERT_EQ(adapter.account.signOut(adapter.account.snapshot().ticket), AuthError::None);
  adapter.signIn(LoginKind::Acquisition);
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(adapter.remote.calls, 0);
  adapter.signIn();
  adapter.evidence.revoke = &adapter.account;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(adapter.remote.calls, 0);
}
TEST_F(LifecycleStatsTest, OutputBudgetAndUnknownCheckpointVersionsFailBeforeSending) {
  LifecycleStats shortOutput{adapter.account,
                             adapter.evidence,
                             adapter.remote,
                             adapter.batch.data(),
                             adapter.batch.size(),
                             adapter.output.data(),
                             1};
  // Exercise via the actual owner token by constraining its injected phase handler.
  struct ShortAdapter : ServerAdapter {
    LifecycleStats& handler;
    explicit ShortAdapter(LifecycleStats& h) : handler(h) {}
    Capabilities capabilities() const override { return {}; }
    Snapshot cachedSnapshot() const override { return {}; }
    CommandStatus deliver(const SyncWriterToken&, Intent) override { return CommandStatus::Unavailable; }
    PhaseOutcome syncPhase(const SyncWriterToken& t, const PhaseRequest& r) override { return handler.run(t, r); }
  } small(shortOutput);
  {
    SyncOwner owner(journal, small, "adapter:one", "account:one", scratch.data(), scratch.size());
    ASSERT_EQ(owner.activate(), SyncResult::Ready);
    EXPECT_EQ(owner.step(), SyncResult::Unavailable);
  }
  EXPECT_EQ(adapter.remote.calls, 0);
  ASSERT_EQ(journal.checkpoint(1, "BST9unknown"), JournalResult::Ok);
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(adapter.remote.calls, 0);
}
TEST(PageCaptureTest, ClockCorrectionsBecomeGapsAndFailedWritesKeepTheQueue) {
  card = {};
  PageJournal journal{"/pages"};
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  booksync::reader::PageCapture capture;
  capture.start({1, 1800000000, 0, 0, 0, 1, 100, 0, 1, 10000, 0});
  capture.accept(1800000010, 10);
  card.readOnly = true;
  EXPECT_NE(capture.flush(journal), JournalResult::Ok);
  EXPECT_EQ(capture.pending(), 1);
  EXPECT_TRUE(capture.hasWork());
  card.readOnly = false;
  ASSERT_EQ(capture.flush(journal), JournalResult::Ok);
  EXPECT_EQ(capture.pending(), 0);
  EXPECT_FALSE(capture.hasWork());
  capture.start({1, 1800000050, 0, 0, 0, 2, 100, 0, 2, 10000, 0});
  capture.accept(1800000001, 10);
  ASSERT_EQ(capture.flush(journal), JournalResult::Ok);
  PageEvent gap;
  ASSERT_EQ(journal.read(1, gap), JournalResult::Ok);
  EXPECT_EQ(gap.flags, PageEvent::Gap);
  capture.start({1, 1800000100, 0, 0, 0, 100, 100, 0, 100, 10000, 0});
  capture.accept(1800000110, 10);
  ASSERT_EQ(capture.flush(journal), JournalResult::Ok);
  ASSERT_EQ(journal.read(2, gap), JournalResult::Ok);
  EXPECT_EQ(gap.flags, PageEvent::Gap);  // End-of-book sentinels are not page events.
}
