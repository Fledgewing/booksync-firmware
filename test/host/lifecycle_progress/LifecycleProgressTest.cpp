#include <HalStorage.h>
#include <gtest/gtest.h>

#include <array>
#include <limits>

#include "booksync/server/bookorbit/Auth.h"
#include "booksync/server/bookorbit/LifecycleMatch.h"
#include "booksync/server/bookorbit/LifecycleProgress.h"
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
class ProgressCapture final : public ProgressEvidence {
 public:
  ProgressInput input{};
  ProgressValues applied{};
  bool fail = false;
  unsigned writes = 0;

  bool load(const JournalEntry&, ProgressInput& out) override {
    out = input;
    return true;
  }
  bool apply(const JournalEntry&, const ProgressInput& in, const ProgressValues& value) override {
    if (fail) return false;
    ++writes;
    applied = value;
    (void)in;
    return true;
  }
};
class Remote final : public ProgressTransport {
 public:
  unsigned calls = 0, commits = 0, reconciles = 0;
  bool committed = false, timeout = false, beforeCommit = false, cut = false, unavailable = false, receiptLost = false;
  Account* revoke = nullptr;
  ProgressQuery sent{};
  ProgressValues values{true, true, 0.5, 1234, "raw/remote", "Synthetic reader", false, false};
  ProgressReply request(const SyncWriterToken&, std::string_view method, std::string_view path, const ProgressQuery& q,
                        bool reconcile) override {
    EXPECT_EQ(method, q.deliver ? "PUT" : "GET");
    EXPECT_EQ(path,
              q.deliver ? "/koreader/syncs/progress" : "/koreader/syncs/progress/0123456789abcdef0123456789abcdef");
    ++calls;
    reconciles += reconcile;
    EXPECT_EQ(q.hash, hash);
    sent = q;
    if (unavailable) return {};
    if (beforeCommit) {
      if (cut) throw PowerCut{};
      return {ProgressResult::Uncertain, {}};
    }
    if (q.deliver && committed && reconcile && receiptLost) return {ProgressResult::Uncertain, {}};
    if (q.deliver && !committed) {
      committed = true;
      ++commits;
    }
    if (revoke) EXPECT_EQ(revoke->signOut(revoke->snapshot().ticket), AuthError::None);
    if (cut) throw PowerCut{};
    if (timeout) return {ProgressResult::Uncertain, {}};
    return {ProgressResult::Ok, values};
  }
};
class Adapter final : public ServerAdapter {
 public:
  Login login;
  Auth auth{login};
  Account account{auth};
  Evidence evidence;
  Remote remote;
  ProgressCapture capture;
  bool stall = false;
  std::array<char, LifecycleJournal::MaxCheckpoint> output{};
  LifecycleProgress state{account, capture, remote, output.data(), output.size()};
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
struct LifecycleProgressTest : testing::Test {
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
    adapter.capture.input.deliver = true;
    adapter.capture.input.percentage = 0.25;
    adapter.capture.input.position = "raw/local";
    adapter.capture.input.capturedAt = 1234;
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
    for (unsigned phase = 0; phase < 4; ++phase) {
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
TEST_F(LifecycleProgressTest, ReplyCheckpointPrecedesPublicationAndFinalAck) {
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(read().acknowledged, 15);
  EXPECT_EQ(adapter.capture.writes, 0);
  EXPECT_EQ(adapter.remote.sent.position, "raw/local");
  EXPECT_DOUBLE_EQ(adapter.remote.sent.percentage, 0.25);
  card.restart();
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(read().acknowledged, 31);
  EXPECT_EQ(adapter.capture.writes, 1);
  EXPECT_EQ(adapter.capture.applied.position, "raw/remote");
  EXPECT_EQ(adapter.remote.calls, 1);
  EXPECT_EQ(step(), SyncResult::Completed);
  EXPECT_EQ(step(), SyncResult::Idle);
}
TEST_F(LifecycleProgressTest, TimeoutAndCrashOnBothSidesOfCommitReconcileWithoutDuplicate) {
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
      EXPECT_EQ(read().acknowledged, 15);
      card.restart();
      adapter.remote.cut = adapter.remote.timeout = adapter.remote.beforeCommit = false;
      EXPECT_EQ(step(), SyncResult::Advanced);
      EXPECT_EQ(step(), SyncResult::Advanced);
      EXPECT_EQ(adapter.remote.commits, 1);
    }
  EXPECT_EQ(adapter.remote.reconciles, 4);
}
TEST_F(LifecycleProgressTest, FetchDoesNotDeliverAndAbsentRemoteIsPublished) {
  adapter.capture.input.deliver = false;
  adapter.remote.values = {};
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_FALSE(adapter.remote.sent.deliver);
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_FALSE(adapter.capture.applied.present);
}
TEST_F(LifecycleProgressTest, MissingPercentageDiffersFromZeroAndResetMetadataSurvives) {
  adapter.remote.values = {true, true, 0, 1250, "0", "web", true, true};
  EXPECT_EQ(step(), SyncResult::Advanced);
  card.restart();
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_TRUE(adapter.capture.applied.percentagePresent);
  EXPECT_DOUBLE_EQ(adapter.capture.applied.percentage, 0);
  EXPECT_TRUE(adapter.capture.applied.reset);
  EXPECT_TRUE(adapter.capture.applied.web);
  EXPECT_EQ(adapter.capture.applied.label, "web");
}
TEST_F(LifecycleProgressTest, PublicationFailureAndCrashAfterPublicationNeverResend) {
  EXPECT_EQ(step(), SyncResult::Advanced);
  adapter.capture.fail = true;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  card.restart();
  adapter.capture.fail = false;
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(adapter.remote.calls, 1);
}
TEST_F(LifecycleProgressTest, AccountAndExactSourceAreRequiredEvenForPendingPublication) {
  adapter.capture.input.scope.account = "other";
  EXPECT_EQ(step(), SyncResult::Unavailable);
  adapter.capture.input.scope.account = "account:one";
  adapter.capture.input.source.revision.bytes[0] = 9;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  adapter.capture.input.source = adapter.evidence.source;
  adapter.capture.input.syncOwner = 9;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(adapter.remote.calls, 0);
  adapter.capture.input.syncOwner = 7;
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(adapter.account.signOut(adapter.account.snapshot().ticket), AuthError::None);
  EXPECT_EQ(step(), SyncResult::Unavailable);
  adapter.signIn(LoginKind::Acquisition);
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(adapter.capture.writes, 0);
}
TEST_F(LifecycleProgressTest, EpochChangeAfterRemoteCommitStaysUncertain) {
  adapter.remote.revoke = &adapter.account;
  EXPECT_EQ(step(), SyncResult::Uncertain);
  EXPECT_EQ(read().acknowledged, 15);
  adapter.remote.revoke = nullptr;
  adapter.signIn();
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(adapter.remote.commits, 1);
}
TEST_F(LifecycleProgressTest, InvalidCaptureAndIncompatibleEditionNeverSend) {
  adapter.capture.input.percentage = std::numeric_limits<double>::quiet_NaN();
  EXPECT_EQ(step(), SyncResult::Unavailable);
  adapter.capture.input.percentage = 1.1;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  adapter.capture.input.percentage = 0;
  adapter.capture.input.capturedAt = 0;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  adapter.capture.input.capturedAt = 1234;
  auto checkpoint = std::string(read().checkpoint);
  checkpoint[84] = 0;
  ASSERT_EQ(journal.checkpoint(1, checkpoint), JournalResult::Ok);
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(adapter.remote.calls, 0);
}
TEST_F(LifecycleProgressTest, InvalidAndOversizedRepliesNeverAckOrTruncate) {
  adapter.remote.values.percentage = std::numeric_limits<double>::infinity();
  EXPECT_EQ(step(), SyncResult::Uncertain);
  adapter.remote.values.percentage = 0.5;
  const std::string large(4096, 'x');
  adapter.remote.values.position = large;
  EXPECT_EQ(step(), SyncResult::Uncertain);
  EXPECT_EQ(read().acknowledged, 15);
  adapter.remote.unavailable = true;
  EXPECT_EQ(step(), SyncResult::Unavailable);
}
TEST_F(LifecycleProgressTest, CheckpointCodecRejectsEveryTruncationAndUnknownFlags) {
  EXPECT_EQ(step(), SyncResult::Advanced);
  const std::string bytes(read().checkpoint);
  std::string_view matched;
  ProgressValues value;
  ASSERT_TRUE(LifecycleProgress::decode(bytes, matched, value));
  for (size_t n = 0; n < bytes.size(); ++n)
    EXPECT_FALSE(LifecycleProgress::decode(std::string_view(bytes).substr(0, n), matched, value));
  auto corrupt = bytes;
  corrupt[4] = char(0x80);
  EXPECT_FALSE(LifecycleProgress::decode(corrupt, matched, value));
  ASSERT_EQ(journal.checkpoint(1, corrupt), JournalResult::Ok);
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(adapter.remote.calls, 1);
}
TEST_F(LifecycleProgressTest, CutsEveryStorageOperationAcrossCheckpointAndAcknowledgement) {
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
      for (int i = 0; i < 3 && read().acknowledged == 15; ++i) step();
      EXPECT_EQ(read().acknowledged, 31);
      EXPECT_EQ(adapter.remote.commits, 1);
    }
}
TEST_F(LifecycleProgressTest, MissingCommitProofRetainsUnknownDeliveryWithoutOverwritingRemote) {
  adapter.remote.timeout = true;
  EXPECT_EQ(step(), SyncResult::Uncertain);
  card.restart();
  adapter.remote.timeout = false;
  adapter.remote.receiptLost = true;
  adapter.remote.values.position = "newer/remote";
  EXPECT_EQ(step(), SyncResult::Uncertain);
  EXPECT_EQ(read().acknowledged, 15);
  EXPECT_EQ(adapter.remote.commits, 1);
  EXPECT_EQ(adapter.capture.writes, 0);
}
TEST_F(LifecycleProgressTest, RawNativeAndCompletedStatsCheckpointRemainByteExact) {
  std::string original(read().checkpoint);
  const std::string native("raw\0unknown\xff", 12);
  original[117] = char(native.size());
  original += native;
  std::string stats(36, '\0');
  stats.replace(0, 4, "BST1");
  for (unsigned i = 0; i < 4; ++i) stats[32 + i] = char(original.size() >> (8 * i));
  stats += original;
  ASSERT_EQ(journal.checkpoint(1, stats), JournalResult::Ok);
  EXPECT_EQ(step(), SyncResult::Advanced);
  card.restart();
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(read().checkpoint, stats);
  MatchCheckpoint identity;
  ASSERT_TRUE(LifecycleMatch::decode(read().checkpoint, identity));
  EXPECT_EQ(identity.native, native);
}
TEST_F(LifecycleProgressTest, RemotePositionWithoutPercentageRemainsAbsentRatherThanZero) {
  adapter.capture.input.deliver = false;
  adapter.remote.values = {true, false, 0, 1200, "raw/remote", {}, false, false};
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_TRUE(adapter.capture.applied.present);
  EXPECT_FALSE(adapter.capture.applied.percentagePresent);
  EXPECT_EQ(adapter.capture.applied.position, "raw/remote");
  EXPECT_EQ(adapter.remote.commits, 0);
}
