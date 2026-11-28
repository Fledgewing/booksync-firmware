#include <HalStorage.h>
#include <gtest/gtest.h>

#include <array>

#include "booksync/server/bookorbit/Auth.h"
#include "booksync/server/bookorbit/LifecycleMatch.h"
#include "booksync/server/bookorbit/LifecycleState.h"
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
class StateCapture final : public StateEvidence {
 public:
  StateInput input{};
  StateValues applied{};
  bool fail = false;
  unsigned writes = 0;
  bool statusAllowed = false;
  bool load(const JournalEntry&, StateInput& out) override {
    out = input;
    return true;
  }
  bool apply(const JournalEntry&, const StateInput& in, const StateValues& value) override {
    if (fail) return false;
    ++writes;
    applied = value;
    statusAllowed = !in.manualStatusPending && in.capturedToken == in.currentToken;
    return true;
  }
};
class Remote final : public StateTransport {
 public:
  unsigned calls = 0, commits = 0, reconciles = 0;
  bool committed = false, timeout = false, beforeCommit = false, cut = false, unavailable = false;
  Account* revoke = nullptr;
  StateQuery sent{};
  StateValues values{true, "complete", "2026-11-27", true, 4, true, true, "Synthetic review"};
  StateReply exchange(const SyncWriterToken&, const StateQuery& q, bool reconcile) override {
    ++calls;
    reconciles += reconcile;
    EXPECT_EQ(q.hash, hash);
    sent = q;
    if (unavailable) return {};
    if (beforeCommit) {
      if (cut) throw PowerCut{};
      return {StateResult::Uncertain, {}};
    }
    if (!committed) {
      committed = true;
      ++commits;
    }
    if (revoke) EXPECT_EQ(revoke->signOut(revoke->snapshot().ticket), AuthError::None);
    if (cut) throw PowerCut{};
    if (timeout) return {StateResult::Uncertain, {}};
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
  StateCapture capture;
  bool stall = false;
  std::array<char, LifecycleJournal::MaxCheckpoint> output{};
  LifecycleState state{account, capture, remote, output.data(), output.size()};
  Capabilities capabilities() const override { return {}; }
  Snapshot cachedSnapshot() const override { return {}; }
  CommandStatus deliver(const SyncWriterToken&, Intent) override { return CommandStatus::Unavailable; }
  PhaseOutcome syncPhase(const SyncWriterToken& t, const PhaseRequest& r) override {
    return stall ? PhaseOutcome::Checkpointed : state.run(t, r);
  }
  void signIn(LoginKind kind = LoginKind::Account) {
    CredentialCapture input;
    char secret[] = "synthetic";
    ASSERT_TRUE(input.capture(kind, "synthetic-origin", "synthetic-user", secret, sizeof(secret) - 1));
    ASSERT_EQ(account.signIn(input, account.snapshot().ticket), AuthError::None);
  }
};
struct LifecycleStateTest : testing::Test {
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
    adapter.capture.input.scope = {"adapter:one", "account:one"};
    adapter.capture.input.syncOwner = 7;
    adapter.capture.input.source = adapter.evidence.source;
    adapter.capture.input.status = "reading";
    // F18a checkpoint fixture; F18b/F18c own the preceding phase implementations.
    std::string checkpoint(LifecycleMatch::HeaderBytes, '\0');
    checkpoint.replace(0, 4, "BSM1");
    checkpoint[4] = 1;
    checkpoint[20] = 2;
    checkpoint[36] = 3;
    checkpoint[68] = 11;
    checkpoint[76] = 22;
    checkpoint[84] = 1;
    checkpoint.replace(85, 32, hash);
    for (unsigned phase = 0; phase < 3; ++phase) {
      ASSERT_EQ(journal.attempt(1, static_cast<LifecyclePhase>(phase)), JournalResult::Ok);
      ASSERT_EQ(journal.acknowledge(1, static_cast<LifecyclePhase>(phase), checkpoint), JournalResult::Ok);
    }
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
TEST_F(LifecycleStateTest, ReplyIsCheckpointedBeforeWritebackAndAckThenNeverSentAgain) {
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(read().acknowledged, 7);
  EXPECT_EQ(adapter.capture.writes, 0);
  card.restart();
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(read().acknowledged, 15);
  EXPECT_EQ(adapter.capture.writes, 1);
  EXPECT_EQ(adapter.capture.applied.rating, 4);
  EXPECT_EQ(adapter.remote.calls, 1);
  EXPECT_EQ(step(), SyncResult::Unavailable);  // progress belongs to F18e
  EXPECT_EQ(adapter.remote.calls, 1);
}
TEST_F(LifecycleStateTest, CompletedStatsWrapperSurvivesStateCheckpointAndAcknowledgement) {
  const std::string original(read().checkpoint);
  std::string stats(36, '\0');  // BST1's fixed header; preserve every byte from the preceding phase.
  stats.replace(0, 4, "BST1");
  stats[4] = stats[8] = stats[12] = 70;
  for (unsigned i = 0; i < 4; ++i) stats[32 + i] = char(original.size() >> (8 * i));
  stats += original;
  ASSERT_EQ(journal.checkpoint(1, stats), JournalResult::Ok);
  ASSERT_EQ(step(), SyncResult::Advanced);
  const std::string pending(read().checkpoint);
  std::string_view matched;
  StateValues values;
  ASSERT_TRUE(LifecycleState::decode(pending, matched, values));
  EXPECT_EQ(matched, stats);
  EXPECT_EQ(adapter.capture.writes, 0);
  card.restart();
  ASSERT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(read().acknowledged, 15);
  EXPECT_EQ(read().checkpoint, stats);
  EXPECT_EQ(adapter.remote.calls, 1);
  EXPECT_EQ(adapter.capture.writes, 1);
}
TEST_F(LifecycleStateTest, TimeoutAndCrashBeforeAndAfterCommitReconcile) {
  const auto seed = card.disk;
  for (bool crash : {false, true})
    for (bool before : {false, true}) {
      card.disk = seed;
      card.restart();
      adapter.remote.committed = false;
      adapter.remote.commits = 0;
      adapter.remote.cut = crash;
      adapter.remote.timeout = !crash;
      adapter.remote.beforeCommit = before;
      if (crash)
        EXPECT_THROW(step(), PowerCut);
      else
        EXPECT_EQ(step(), SyncResult::Uncertain);
      EXPECT_EQ(read().acknowledged, 7);
      card.restart();
      adapter.remote.cut = adapter.remote.timeout = adapter.remote.beforeCommit = false;
      EXPECT_EQ(step(), SyncResult::Advanced);
      EXPECT_EQ(step(), SyncResult::Advanced);
      EXPECT_EQ(adapter.remote.commits, 1);
    }
  EXPECT_EQ(adapter.remote.reconciles, 4);
}
TEST_F(LifecycleStateTest, AbsentRatingAndReviewAreNotClearsWithoutKnownAccountHistory) {
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(adapter.remote.sent.rating, 0);
  EXPECT_FALSE(adapter.remote.sent.ratingCleared);
  EXPECT_FALSE(adapter.remote.sent.reviewPresent);
  EXPECT_FALSE(adapter.remote.sent.reviewCleared);
}
TEST_F(LifecycleStateTest, KnownHistoryAllowsExplicitClearsAndModifiedFallback) {
  adapter.capture.input.known = {false, {}, {}, true, 4, true, true, "Old review"};
  adapter.capture.input.statusModified = "2026-11-26";
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_TRUE(adapter.remote.sent.ratingCleared);
  EXPECT_TRUE(adapter.remote.sent.reviewCleared);
  EXPECT_EQ(adapter.remote.sent.reviewModified, "2026-11-26");
}
TEST_F(LifecycleStateTest, ExplicitValuesAndManualStatusOrdering) {
  adapter.capture.input.manualStatusPending = true;
  adapter.capture.input.rating = 5;
  adapter.capture.input.reviewPresent = true;
  adapter.capture.input.reviewNote = "";  // explicit empty review remains a value
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_TRUE(adapter.remote.sent.status.empty());
  EXPECT_EQ(adapter.remote.sent.rating, 5);
  EXPECT_TRUE(adapter.remote.sent.reviewPresent);
  EXPECT_FALSE(adapter.remote.sent.reviewCleared);
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_FALSE(adapter.capture.statusAllowed);
  EXPECT_FALSE(adapter.capture.applied.statusKnown);
}
TEST_F(LifecycleStateTest, OpenBookDefersWritebackAcrossRestartsWithoutReexchange) {
  adapter.capture.input.open = true;
  EXPECT_EQ(step(), SyncResult::Advanced);
  card.restart();
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(adapter.capture.writes, 0);
  adapter.capture.input.open = false;
  adapter.capture.input.capturedToken = "old";
  adapter.capture.input.currentToken = "new";
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_FALSE(adapter.capture.statusAllowed);
  EXPECT_FALSE(adapter.capture.applied.statusKnown);
  EXPECT_EQ(adapter.remote.calls, 1);
}
TEST_F(LifecycleStateTest, UnknownAndExplicitlyClearedRepliesStayDistinct) {
  adapter.remote.values = {false, {}, {}, true, 0, true, false, {}};
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_TRUE(adapter.capture.applied.ratingKnown);
  EXPECT_EQ(adapter.capture.applied.rating, 0);
  EXPECT_TRUE(adapter.capture.applied.reviewKnown);
  EXPECT_FALSE(adapter.capture.applied.reviewSet);
}
TEST_F(LifecycleStateTest, WrongScopeRevisionAndLoginNeverSend) {
  adapter.capture.input.scope.account = "other";
  EXPECT_EQ(step(), SyncResult::Unavailable);
  adapter.capture.input.scope.account = "account:one";
  adapter.capture.input.source.revision.bytes[0] = 9;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  adapter.capture.input.source = adapter.evidence.source;
  adapter.capture.input.syncOwner = 8;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(adapter.remote.calls, 0);
}
TEST_F(LifecycleStateTest, EpochChangeCannotCheckpointRemoteReply) {
  adapter.remote.revoke = &adapter.account;
  EXPECT_EQ(step(), SyncResult::Uncertain);
  EXPECT_EQ(read().acknowledged, 7);
  adapter.remote.revoke = nullptr;
  adapter.signIn();
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(adapter.remote.commits, 1);
}
TEST_F(LifecycleStateTest, FailedWritebackRetainsCheckpointAndNeverReexchanges) {
  EXPECT_EQ(step(), SyncResult::Advanced);
  adapter.capture.fail = true;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  card.restart();
  adapter.capture.fail = false;
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(adapter.remote.calls, 1);
}
TEST_F(LifecycleStateTest, MalformedOversizedAndUnavailableRemainPending) {
  adapter.capture.input.rating = 6;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(adapter.remote.calls, 0);
  adapter.capture.input.rating = 0;
  adapter.remote.unavailable = true;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  adapter.remote.unavailable = false;
  adapter.remote.values.rating = 9;
  EXPECT_EQ(step(), SyncResult::Uncertain);
  adapter.remote.values.rating = 4;
  const std::string large(1024, 'x');
  adapter.remote.values.reviewNote = large;
  EXPECT_EQ(step(), SyncResult::Uncertain);
  EXPECT_EQ(read().acknowledged, 7);
}
TEST_F(LifecycleStateTest, CutsEveryJournalOperationBeforeCheckpointAndBeforeAck) {
  const auto seed = card.disk;
  size_t operations = 0;
  {
    SyncOwner owner(journal, adapter, "adapter:one", "account:one", scratch.data(), scratch.size());
    ASSERT_EQ(owner.activate(), SyncResult::Ready);
    card.events.clear();
    ASSERT_EQ(owner.step(), SyncResult::Advanced);
    ASSERT_EQ(owner.step(), SyncResult::Advanced);
    operations = card.events.size();
  }
  for (bool eager : {false, true})
    for (size_t cut = 0; cut < operations; ++cut) {
      SCOPED_TRACE(cut);
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
        EXPECT_THROW(
            {
              owner.step();
              owner.step();
            },
            PowerCut);
      }
      card.restart();
      ASSERT_EQ(journal.recover(), JournalResult::Ok);
      for (int i = 0; i < 3 && read().acknowledged == 7; ++i) step();
      EXPECT_EQ(read().acknowledged, 15);
      EXPECT_EQ(adapter.remote.commits, 1);
    }
}

TEST_F(LifecycleStateTest, NativeUnreadReadingAndFinishedStatusesPassThrough) {
  const auto seed = card.disk;
  for (auto status : {"new", "reading", "complete"}) {
    card.disk = seed;
    card.restart();
    adapter.capture.input.status = status;
    EXPECT_EQ(step(), SyncResult::Advanced);
    EXPECT_EQ(adapter.remote.sent.status, status);
  }
}
TEST_F(LifecycleStateTest, UnknownReplyConfirmsNoWritebackAndNoInventedClears) {
  adapter.remote.values = {};
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(read().acknowledged, 15);
  EXPECT_EQ(adapter.capture.writes, 0);
}
TEST_F(LifecycleStateTest, PendingCheckpointRejectsTruncationBadFlagsAndLengths) {
  EXPECT_EQ(step(), SyncResult::Advanced);
  const std::string bytes(read().checkpoint);
  std::string_view matched;
  StateValues state;
  ASSERT_TRUE(LifecycleState::decode(bytes, matched, state));
  for (size_t length = 0; length < bytes.size(); ++length)
    EXPECT_FALSE(LifecycleState::decode(std::string_view(bytes).substr(0, length), matched, state));
  auto corrupt = bytes;
  corrupt[4] = char(0x80);
  EXPECT_FALSE(LifecycleState::decode(corrupt, matched, state));
  corrupt = bytes;
  corrupt[6] = 0;
  EXPECT_FALSE(LifecycleState::decode(corrupt, matched, state));
  corrupt = bytes;
  corrupt[5] = 6;
  EXPECT_FALSE(LifecycleState::decode(corrupt, matched, state));
  ASSERT_EQ(journal.checkpoint(1, corrupt), JournalResult::Ok);
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(adapter.remote.calls, 1);
}
TEST_F(LifecycleStateTest, SignOutAndAcquisitionOnlyCannotSendOrApplyPendingState) {
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(adapter.account.signOut(adapter.account.snapshot().ticket), AuthError::None);
  EXPECT_EQ(step(), SyncResult::Unavailable);
  adapter.signIn(LoginKind::Acquisition);
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(adapter.remote.calls, 1);
  EXPECT_EQ(adapter.capture.writes, 0);
}

TEST_F(LifecycleStateTest, InsufficientPendingCapacityAndOversizeCaptureNeverSend) {
  const std::string original(read().checkpoint);
  std::string matched(original);
  matched.resize(LifecycleJournal::MaxCheckpoint - LifecycleState::HeaderBytes + 1, 'x');
  const size_t nativeSize = matched.size() - LifecycleMatch::HeaderBytes;
  matched[117] = static_cast<char>(nativeSize);
  matched[118] = static_cast<char>(nativeSize >> 8);
  ASSERT_EQ(journal.checkpoint(1, matched), JournalResult::Ok);
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(adapter.remote.calls, 0);
  ASSERT_EQ(journal.checkpoint(1, original), JournalResult::Ok);
  const std::string huge(LifecycleState::MaxReviewBytes + 1, 'x');
  adapter.capture.input.reviewPresent = true;
  adapter.capture.input.reviewNote = huge;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(adapter.remote.calls, 0);
}

TEST_F(LifecycleStateTest, RawUnknownStatusStillPublishesUnknownHistoryFlags) {
  adapter.capture.input.known.ratingKnown = true;
  adapter.capture.input.known.rating = 4;
  adapter.remote.values = {false, "new", {}, false, 0, false, false, {}};
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(read().acknowledged, 7);
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(adapter.capture.writes, 1);
  EXPECT_FALSE(adapter.capture.applied.statusKnown);
  EXPECT_FALSE(adapter.capture.applied.ratingKnown);
}

TEST_F(LifecycleStateTest, UnchangedIncompleteCheckpointCannotSpinTheDrain) {
  adapter.stall = true;
  EXPECT_EQ(step(), SyncResult::StorageError);
  EXPECT_EQ(read().acknowledged, 7);
  EXPECT_EQ(adapter.remote.calls, 0);
  card.restart();
  EXPECT_EQ(step(), SyncResult::StorageError);
}
