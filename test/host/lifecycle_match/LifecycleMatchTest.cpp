#include <HalStorage.h>
#include <gtest/gtest.h>

#include <array>

#include "booksync/server/bookorbit/Auth.h"
#include "booksync/server/bookorbit/LifecycleMatch.h"
#include "booksync/services/SyncOwner.h"
using namespace booksync;
using namespace booksync::storage;
using namespace booksync::server;
namespace {
constexpr PublishPaths paths{"/match", "/match.tmp", "/match.bak"};
constexpr std::string_view hash = "0123456789abcdef0123456789abcdef";
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
  std::array<BindingView, 2> rows{};
  MatchInput input{};
  Account* revoke = nullptr;
  Evidence() {
    source.book.bytes[0] = 1;
    source.edition.bytes[0] = 2;
    source.revision.bytes[0] = 3;
    rows[0] = {{"adapter:one", "account:one"}, {}, "/book.epub", source};
    input = {rows[0].scope, 7, source, "/book.epub", hash, "Synthetic title", "Synthetic author", 1234, rows.data(), 1};
  }
  bool load(const JournalEntry&, MatchInput& out) override {
    out = input;
    if (revoke) EXPECT_EQ(revoke->signOut(revoke->snapshot().ticket), AuthError::None);
    return true;
  }
};
class Remote final : public MatchTransport {
 public:
  unsigned calls = 0, commits = 0, reconciles = 0;
  bool committed = false, timeout = false, beforeCommit = false, cut = false;
  Account* revoke = nullptr;
  std::array<MatchRow, 2> rows{{{hash, 11, 22, true}, {hash, 12, 23, false}}};
  size_t count = 1;
  MatchReply check(const SyncWriterToken&, const MatchQuery& q, bool reconcile) override {
    ++calls;
    reconciles += reconcile;
    EXPECT_EQ(q.hash, hash);
    EXPECT_EQ(q.source, "current_file");
    EXPECT_EQ(q.title, "Synthetic title");
    EXPECT_EQ(q.authors, "Synthetic author");
    EXPECT_EQ(q.lastOpen, 1234);
    if (beforeCommit) {
      if (cut) throw PowerCut{};
      return {MatchResult::Uncertain};
    }
    if (!committed) {
      committed = true;
      ++commits;
    }
    if (revoke) EXPECT_EQ(revoke->signOut(revoke->snapshot().ticket), AuthError::None);
    if (cut) throw PowerCut{};
    if (timeout) return {MatchResult::Uncertain};
    return {MatchResult::Ok, rows.data(), count};
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
  LifecycleMatch match{account, evidence, remote, output.data(), output.size()};
  Capabilities capabilities() const override { return {}; }
  Snapshot cachedSnapshot() const override { return {}; }
  CommandStatus deliver(const SyncWriterToken&, Intent) override { return CommandStatus::Unavailable; }
  PhaseOutcome syncPhase(const SyncWriterToken& t, const PhaseRequest& r) override { return match.run(t, r); }
  void signIn(LoginKind kind = LoginKind::Account) {
    CredentialCapture input;
    char secret[] = "synthetic";
    ASSERT_TRUE(input.capture(kind, "synthetic-origin", "synthetic-user", secret, sizeof(secret) - 1));
    ASSERT_EQ(account.signIn(input, account.snapshot().ticket), AuthError::None);
  }
};
struct LifecycleMatchTest : testing::Test {
  LifecycleJournal journal{paths};
  Adapter adapter;
  std::array<char, SyncOwner::ScratchBytes> scratch{};
  void SetUp() override {
    card = {};
    ASSERT_EQ(journal.recover(), JournalResult::Ok);
    uint64_t id = 0;
    ASSERT_EQ(journal.enqueue({0, "adapter:one", "account:one", "local:one", "capture", "native", 0}, id),
              JournalResult::Ok);
    adapter.signIn();
  }
  JournalEntry read() {
    JournalEntry e;
    EXPECT_EQ(journal.read(1, e, scratch.data(), scratch.size()), JournalResult::Ok);
    return e;
  }
  SyncResult step() {
    SyncOwner owner(journal, adapter, "adapter:one", "account:one", scratch.data(), scratch.size());
    EXPECT_EQ(owner.activate(), SyncResult::Ready);
    return owner.step();
  }
};
}  // namespace
TEST_F(LifecycleMatchTest, CommitsIdentityAndNativeCheckpointTogetherThenStopsAtStats) {
  EXPECT_EQ(step(), SyncResult::Advanced);
  const auto e = read();
  MatchCheckpoint saved;
  ASSERT_TRUE(LifecycleMatch::decode(e.checkpoint, saved));
  EXPECT_EQ(saved.source, adapter.evidence.source);
  EXPECT_EQ(saved.bookId, 11);
  EXPECT_EQ(saved.bookFileId, 22);
  EXPECT_EQ(saved.hash, hash);
  EXPECT_TRUE(saved.positionCompatible);
  EXPECT_EQ(saved.native, "native");
  EXPECT_EQ(e.acknowledged, 1);
  const std::string matched(e.checkpoint);
  for (unsigned boot = 0; boot < 2; ++boot) {
    card.restart();
    EXPECT_EQ(step(), SyncResult::Unavailable);
    const auto pending = read();
    EXPECT_EQ(pending.acknowledged, 1);
    EXPECT_EQ(pending.attempting, 2);  // F17 durably attempts the next, unavailable phase.
    EXPECT_EQ(pending.checkpoint, matched);
  }
  EXPECT_EQ(adapter.remote.calls, 1);
}
TEST_F(LifecycleMatchTest, TimeoutBeforeCommitRestartsAndReconcilesWithoutInventingAck) {
  adapter.remote.beforeCommit = true;
  EXPECT_EQ(step(), SyncResult::Uncertain);
  EXPECT_EQ(read().acknowledged, 0);
  EXPECT_EQ(read().checkpoint, "native");
  card.restart();
  adapter.remote.beforeCommit = false;
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(adapter.remote.commits, 1);
  EXPECT_EQ(adapter.remote.reconciles, 1);
}
TEST_F(LifecycleMatchTest, TimeoutAfterCommitRestartsAndDeduplicates) {
  adapter.remote.timeout = true;
  EXPECT_EQ(step(), SyncResult::Uncertain);
  EXPECT_EQ(adapter.remote.commits, 1);
  card.restart();
  adapter.remote.timeout = false;
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(adapter.remote.commits, 1);
  EXPECT_EQ(adapter.remote.reconciles, 1);
}
TEST_F(LifecycleMatchTest, CrashOnEitherSideOfRemoteCommitReconciles) {
  const auto seed = card.disk;
  for (bool before : {false, true}) {
    card.disk = seed;
    card.restart();
    adapter.remote.committed = false;
    adapter.remote.commits = 0;
    adapter.remote.cut = true;
    adapter.remote.beforeCommit = before;
    EXPECT_THROW(step(), PowerCut);
    card.restart();
    adapter.remote.cut = false;
    adapter.remote.beforeCommit = false;
    EXPECT_EQ(step(), SyncResult::Advanced);
    EXPECT_EQ(adapter.remote.commits, 1);
  }
}
TEST_F(LifecycleMatchTest, MissingWrongHashAndAmbiguousRepliesStayPending) {
  adapter.remote.count = 0;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  adapter.remote.count = 1;
  adapter.remote.rows[0].hash = "ffffffffffffffffffffffffffffffff";
  EXPECT_EQ(step(), SyncResult::Unavailable);
  adapter.remote.rows[0].hash = hash;
  adapter.remote.count = 2;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(read().acknowledged, 0);
}
TEST_F(LifecycleMatchTest, ExactDuplicateRowsAreHarmlessAndIncompatibleEditionIsRecorded) {
  adapter.remote.rows[0].positionCompatible = false;
  adapter.remote.rows[1] = adapter.remote.rows[0];
  adapter.remote.count = 2;
  EXPECT_EQ(step(), SyncResult::Advanced);
  MatchCheckpoint saved;
  ASSERT_TRUE(LifecycleMatch::decode(read().checkpoint, saved));
  EXPECT_FALSE(saved.positionCompatible);
}
TEST_F(LifecycleMatchTest, AccountMismatchSignOutAndAcquisitionOnlyNeverSend) {
  adapter.evidence.input.syncOwner = 8;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  adapter.evidence.input.syncOwner = 7;
  EXPECT_EQ(adapter.account.signOut(adapter.account.snapshot().ticket), AuthError::None);
  EXPECT_EQ(step(), SyncResult::Unavailable);
  adapter.signIn(LoginKind::Acquisition);
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(adapter.remote.calls, 0);
}
TEST_F(LifecycleMatchTest, EpochChangeDuringRequestCannotAcknowledge) {
  adapter.remote.revoke = &adapter.account;
  EXPECT_EQ(step(), SyncResult::Uncertain);
  EXPECT_EQ(read().acknowledged, 0);
  EXPECT_EQ(read().checkpoint, "native");
  adapter.remote.revoke = nullptr;
  adapter.signIn();
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(adapter.remote.commits, 1);
}
TEST_F(LifecycleMatchTest, AmbiguousLocalEditionRevisionAndScopeNeverSend) {
  adapter.evidence.rows[1] = adapter.evidence.rows[0];
  adapter.evidence.rows[1].source.edition.bytes[0] = 9;
  adapter.evidence.input.count = 2;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  adapter.evidence.input.count = 1;
  adapter.evidence.input.source.revision.bytes[0] = 9;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  adapter.evidence.input.source = adapter.evidence.source;
  adapter.evidence.input.scope.account = "other";
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(adapter.remote.calls, 0);
}
TEST_F(LifecycleMatchTest, InsufficientCheckpointCapacityNeverSends) {
  adapter.output.fill(0);
  ASSERT_EQ(journal.checkpoint(1, std::string(1000, 'x')), JournalResult::Ok);
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(adapter.remote.calls, 0);
}
TEST_F(LifecycleMatchTest, CutsEveryLocalPublicationOperationAndDeduplicatesRemoteCommit) {
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
      SCOPED_TRACE(eager);
      card.disk = seed;
      card.restart();
      card.eager = eager;
      adapter.remote.committed = false;
      adapter.remote.commits = 0;
      {
        SyncOwner owner(journal, adapter, "adapter:one", "account:one", scratch.data(), scratch.size());
        ASSERT_EQ(owner.activate(), SyncResult::Ready);
        card.events.clear();
        card.stop = cut;
        EXPECT_THROW(owner.step(), PowerCut);
      }
      card.restart();
      ASSERT_EQ(journal.recover(), JournalResult::Ok);
      auto e = read();
      if (!(e.acknowledged & 1)) EXPECT_EQ(step(), SyncResult::Advanced);
      EXPECT_EQ(adapter.remote.commits, 1);
      EXPECT_EQ(read().acknowledged, 1);
    }
}

TEST_F(LifecycleMatchTest, ExistingCanonicalOrEditionAliasesCannotBeRebound) {
  BookAliasBindingView alias{adapter.evidence.input.scope, {"book", "11"}, adapter.evidence.source.book};
  alias.book.bytes[0] = 9;
  adapter.evidence.input.aliases = &alias;
  adapter.evidence.input.aliasCount = 1;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  alias.book = adapter.evidence.source.book;
  adapter.evidence.rows[1] = adapter.evidence.rows[0];
  adapter.evidence.rows[1].alias = {"book-file", "22"};
  adapter.evidence.rows[1].path = "/other.epub";
  adapter.evidence.rows[1].source.edition.bytes[0] = 9;
  adapter.evidence.input.count = 2;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(read().acknowledged, 0);
}
TEST_F(LifecycleMatchTest, InvalidAndOversizedRepliesAndMalformedCheckpointsAreRejected) {
  adapter.remote.rows[0].bookId = 0;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  adapter.remote.rows[0].bookId = 11;
  adapter.remote.count = 9;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  adapter.remote.count = 1;
  EXPECT_EQ(step(), SyncResult::Advanced);
  std::string bytes(read().checkpoint);
  MatchCheckpoint saved;
  for (size_t size = 0; size < LifecycleMatch::HeaderBytes; ++size)
    EXPECT_FALSE(LifecycleMatch::decode(std::string_view(bytes).substr(0, size), saved));
  bytes[84] = 2;
  EXPECT_FALSE(LifecycleMatch::decode(bytes, saved));
}

TEST_F(LifecycleMatchTest, EpochChangeWhileLoadingEvidenceNeverSends) {
  adapter.evidence.revoke = &adapter.account;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(adapter.remote.calls, 0);
}
