#include <HalStorage.h>
#include <gtest/gtest.h>

#include <array>
#include <cstring>

#include "booksync/server/bookorbit/Auth.h"
#include "booksync/server/bookorbit/LifecycleAnnotations.h"
#include "booksync/services/SyncOwner.h"
using namespace booksync;
using namespace booksync::server;
using namespace booksync::storage;
namespace {
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
class Workspace final : public AnnotationWorkspace {
 public:
  AnnotationCapture capture;
  AnnotationProgress progress[2]{};
  AnnotationPending pending[2]{};
  AnnotationRow rows[17]{};
  AnnotationChange remoteRows[3]{};
  AnnotationKey key{hash, "2026-11-27 10:00:00"};
  size_t count = 1;
  bool last = true, fail = false, known = false;
  bool failStage = false, failApply = false, failFinish = false, failKnown = false;
  unsigned applications = 0, stages = 0;
  AnnotationReceipt receipts[3]{};
  AnnotationChangeReader* remoteReader = nullptr;
  class ReceiptReader final : public AnnotationReceiptReader {
   public:
    AnnotationChangeReader* source = nullptr;
    AnnotationChange current;
    AnnotationReceipt receipt;
    const AnnotationReceipt* at(size_t i) override {
      const auto* c = source->at(i);
      if (!c) return nullptr;
      current = *c;
      receipt = {&current, true, false};
      return &receipt;
    }
  } receiptReader;
  bool begin(const JournalEntry& e, const MatchCheckpoint& m, AnnotationCapture& out) override {
    EXPECT_EQ(e.owner, "account:one");
    EXPECT_EQ(m.hash, hash);
    out = capture;
    return restore() && !fail;
  }
  bool load(AnnotationKind kind, AnnotationProgress& p, AnnotationPending& q) override {
    if (!restore()) return false;
    p = progress[unsigned(kind)];
    q = pending[unsigned(kind)];
    return !fail;
  }
  bool batch(AnnotationKind, uint32_t, AnnotationBatch& out) override {
    out = {rows, count, &key, 1, last};
    return !fail;
  }
  bool stage(AnnotationKind kind, const AnnotationReply& r, AnnotationProgress next) override {
    if (failStage) return false;
    ++stages;
    pending[unsigned(kind)] = {true, next, remoteRows, r.count, remoteReader};
    return save() && !fail;
  }
  bool apply(AnnotationKind, const AnnotationPending& p, AnnotationApplied& out) override {
    EXPECT_TRUE(capture.closed && capture.present);
    ++applications;
    if (p.reader) {
      receiptReader.source = p.reader;
      out = {nullptr, p.count, &receiptReader};
    } else {
      for (size_t i = 0; i < p.count; ++i) receipts[i] = {&p.changes[i], true, false};
      out = {receipts, p.count};
    }
    return !fail && !failApply;
  }
  bool finish(AnnotationKind kind, AnnotationProgress next) override {
    if (fail || failFinish) return false;
    progress[unsigned(kind)] = next;
    pending[unsigned(kind)] = {};
    return save();
  }
  // Durable synthetic workspace independent of the F17 journal. Fixed remote
  // rows stand in for immutable decoded payloads; pending/count/cursor survive
  // actual card restart and every FilePublish fault point.
  static bool valid(FsFile& f, void*) {
    char b[16];
    return f.fileSize() == 16 && f.read(b, 16) == 16 && b[0] == 'A' && b[1] == '1';
  }
  bool restore() {
    const PublishPaths paths{"/workspace", "/workspace.tmp", "/workspace.bak"};
    const auto selected = FilePublish::select(paths, valid);
    if (selected == PublishedFile::Missing) {
      for (unsigned i = 0; i < 2; ++i) {
        progress[i] = {};
        pending[i] = {};
      }
      return true;
    }
    if (selected == PublishedFile::Unreadable) return false;
    auto f = Storage.open(selected == PublishedFile::Current ? paths.current : paths.backup, O_RDONLY);
    char b[16]{};
    const bool ok = f && f.read(b, 16) == 16;
    const bool closed = f.close();
    if (!ok || !closed) return false;
    for (unsigned i = 0; i < 2; ++i) {
      const unsigned at = 2 + i * 7;
      progress[i] = {static_cast<uint8_t>(b[at]), bool(b[at + 1])};
      pending[i] = {bool(b[at + 2]),
                    {static_cast<uint8_t>(b[at + 3]), bool(b[at + 4])},
                    remoteRows,
                    static_cast<uint8_t>(b[at + 5]),
                    remoteReader};
    }
    return true;
  }
  bool save() {
    char b[16] = {'A', '1'};
    for (unsigned i = 0; i < 2; ++i) {
      const unsigned at = 2 + i * 7;
      b[at] = progress[i].cursor;
      b[at + 1] = progress[i].done;
      b[at + 2] = pending[i].exists;
      b[at + 3] = pending[i].next.cursor;
      b[at + 4] = pending[i].next.done;
      b[at + 5] = pending[i].count;
    }
    FilePublish f({"/workspace", "/workspace.tmp", "/workspace.bak"}, valid);
    return f.begin(sizeof(b)) && f.write(b, sizeof(b)) && f.finish();
  }
  bool markKnown() override {
    known = true;
    return !fail && !failKnown;
  }
};
class Remote final : public AnnotationTransport {
 public:
  Workspace& work;
  explicit Remote(Workspace& w) : work(w) {}
  unsigned calls = 0, commits = 0, acks = 0, reconciles = 0;
  bool committed[2]{}, before = false, timeout = false, cut = false, ackTimeout = false;
  bool expectKeys = false;
  size_t remoteCount = 0;
  Account* revoke = nullptr;
  AnnotationReply exchange(const SyncWriterToken&, AnnotationKind k, std::string_view h, const AnnotationBatch& batch,
                           bool complete, bool reconcile) override {
    ++calls;
    reconciles += reconcile;
    EXPECT_EQ(h, hash);
    EXPECT_EQ(complete, expectKeys);
    EXPECT_EQ(batch.keyCount, complete && work.progress[unsigned(k)].cursor == 0 ? 1 : 0);
    if (before) {
      if (cut) throw PowerCut{};
      return {MatchResult::Uncertain};
    }
    if (!committed[unsigned(k)]) {
      committed[unsigned(k)] = true;
      ++commits;
    }
    if (revoke) revoke->signOut(revoke->snapshot().ticket);
    if (cut) throw PowerCut{};
    if (timeout) return {MatchResult::Uncertain};
    return {MatchResult::Ok, work.remoteRows, remoteCount, false, work.remoteReader};
  }
  MatchResult acknowledge(const SyncWriterToken&, AnnotationKind, std::string_view,
                          const AnnotationApplied& applied) override {
    for (size_t i = 0; i < applied.count; ++i) {
      const auto* receipt = applied.reader ? applied.reader->at(i) : applied.rows + i;
      EXPECT_FALSE(receipt->verified);
    }
    EXPECT_GT(work.applications, 0);
    ++acks;
    return ackTimeout ? MatchResult::Uncertain : MatchResult::Ok;
  }
};
class Adapter final : public ServerAdapter {
 public:
  Login login;
  Auth auth{login};
  Account account{auth};
  Workspace workspace;
  Remote remote{workspace};
  LifecycleAnnotations annotations{account, workspace, remote};
  Capabilities capabilities() const override { return {}; }
  Snapshot cachedSnapshot() const override { return {}; }
  CommandStatus deliver(const SyncWriterToken&, Intent) override { return CommandStatus::Unavailable; }
  PhaseOutcome syncPhase(const SyncWriterToken& t, const PhaseRequest& r) override { return annotations.run(t, r); }
  void signIn() {
    CredentialCapture c;
    char secret[] = "synthetic";
    ASSERT_TRUE(c.capture(LoginKind::Account, "synthetic-origin", "synthetic-user", secret, sizeof(secret) - 1));
    ASSERT_EQ(account.signIn(c, account.snapshot().ticket), AuthError::None);
  }
};
class AnnotationsTest : public testing::Test {
 public:
  LifecycleJournal journal{{"/annotations", "/annotations.tmp", "/annotations.bak"}};
  Adapter adapter;
  std::array<char, SyncOwner::ScratchBytes> scratch{};
  std::string matched;
  void SetUp() override {
    card = {};
    adapter.signIn();
    auto& c = adapter.workspace.capture;
    c.scope = {"adapter:one", "account:one"};
    c.syncOwner = 7;
    c.source.book.bytes[0] = 1;
    c.source.edition.bytes[0] = 2;
    c.source.revision.bytes[0] = 3;
    c.closed = c.present = true;
    auto& r = adapter.workspace.rows[0];
    r.datetime = "2026-11-27 10:00:00";
    r.start = "/p1";
    r.end = "/p2";
    r.format = "xpointer";
    for (unsigned i = 0; i < 3; ++i) {
      adapter.workspace.remoteRows[i].row = r;
      adapter.workspace.remoteRows[i].row.serverId = i + 1;
      adapter.workspace.remoteRows[i].op = static_cast<AnnotationOp>(i);
    }
    matched.assign(LifecycleMatch::HeaderBytes, '\0');
    matched.replace(0, 4, "BSM1");
    std::memcpy(matched.data() + 4, c.source.book.bytes.data(), 16);
    std::memcpy(matched.data() + 20, c.source.edition.bytes.data(), 16);
    std::memcpy(matched.data() + 36, c.source.revision.bytes.data(), 32);
    matched[68] = 11;
    matched[76] = 22;
    matched[84] = 1;
    matched.replace(85, 32, hash);
    ASSERT_EQ(journal.recover(), JournalResult::Ok);
    uint64_t id = 0;
    ASSERT_EQ(journal.enqueue({0, "adapter:one", "account:one", "local:one", "capture", matched, 0}, id),
              JournalResult::Ok);
    ASSERT_EQ(journal.acknowledge(id, LifecyclePhase::Match, matched), JournalResult::Ok);
    ASSERT_EQ(journal.acknowledge(id, LifecyclePhase::Stats, matched), JournalResult::Ok);
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
};
}  // namespace
TEST_F(AnnotationsTest, ExchangesBothKindsAndPreservesMatchingCheckpoint) {
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(adapter.remote.commits, 2);
  EXPECT_EQ(read().acknowledged, 7);
  EXPECT_EQ(read().checkpoint, matched);
  EXPECT_TRUE(adapter.workspace.known);
  card.restart();
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(adapter.remote.calls, 2);
}
TEST_F(AnnotationsTest, CrashBeforeAndAfterCommitReconcilesFrozenBatch) {
  const auto seed = card.disk;
  for (bool before : {true, false}) {
    card.disk = seed;
    card.restart();
    adapter.workspace.progress[0] = {};
    adapter.workspace.progress[1] = {};
    adapter.remote.committed[0] = adapter.remote.committed[1] = false;
    adapter.remote.commits = 0;
    adapter.remote.before = before;
    adapter.remote.cut = true;
    EXPECT_THROW(step(), PowerCut);
    EXPECT_EQ(read().acknowledged, 3);
    card.restart();
    adapter.remote.cut = adapter.remote.before = false;
    EXPECT_EQ(step(), SyncResult::Advanced);
    EXPECT_EQ(adapter.remote.commits, 2);
  }
}
TEST_F(AnnotationsTest, TimeoutOnEitherSideOfCommitNeverInventsAck) {
  adapter.remote.before = true;
  EXPECT_EQ(step(), SyncResult::Uncertain);
  EXPECT_EQ(read().acknowledged, 3);
  card.restart();
  adapter.remote.before = false;
  adapter.remote.timeout = true;
  EXPECT_EQ(step(), SyncResult::Uncertain);
  EXPECT_EQ(adapter.remote.commits, 1);
  card.restart();
  adapter.remote.timeout = false;
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(adapter.remote.commits, 2);
  EXPECT_EQ(adapter.remote.reconciles, 3);
}
TEST_F(AnnotationsTest, PendingApplyWaitsForCloseEvenWhenSyncDisabled) {
  auto& w = adapter.workspace;
  w.capture.enabled = false;
  w.capture.closed = false;
  w.pending[0] = {true, {1, true}, w.remoteRows, 1};
  ASSERT_TRUE(w.save());
  EXPECT_EQ(step(), SyncResult::Uncertain);
  EXPECT_EQ(adapter.remote.acks, 0);
  EXPECT_EQ(w.applications, 0);
  w.capture.closed = true;
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(adapter.remote.calls, 0);
  EXPECT_EQ(adapter.remote.acks, 1);
  EXPECT_FALSE(w.known);
}
TEST_F(AnnotationsTest, AckTimeoutRetainsPendingAndNeverRepeatsExchange) {
  adapter.remote.remoteCount = 1;
  adapter.remote.ackTimeout = true;
  EXPECT_EQ(step(), SyncResult::Uncertain);
  EXPECT_TRUE(adapter.workspace.pending[0].exists);
  EXPECT_EQ(step(), SyncResult::Uncertain);
  EXPECT_EQ(adapter.remote.acks, 1);
  card.restart();
  adapter.remote.ackTimeout = false;
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(adapter.remote.calls, 2);
  EXPECT_EQ(adapter.remote.commits, 2);
  EXPECT_EQ(adapter.remote.acks, 3);
}
TEST_F(AnnotationsTest, AbsenceRequiresCurrentClosedKnownNativeCapture) {
  auto& c = adapter.workspace.capture;
  c.native = c.syncKnown = true;
  adapter.remote.expectKeys = true;
  EXPECT_EQ(step(), SyncResult::Advanced);
}
TEST_F(AnnotationsTest, HistoricalSnapshotsNeverEstablishDeletion) {
  adapter.workspace.capture.syncKnown = true;
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_FALSE(adapter.remote.expectKeys);
}
TEST_F(AnnotationsTest, DisabledOrIncompatibleNeverStartsExchange) {
  adapter.workspace.capture.legacyEnabled = false;
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(adapter.remote.calls, 0);
}
TEST_F(AnnotationsTest, ScopeAndSourceMismatchNeverSends) {
  adapter.workspace.capture.scope.account = "other";
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(adapter.remote.calls, 0);
  adapter.workspace.capture.scope.account = "account:one";
  adapter.workspace.capture.source.edition.bytes[0] = 9;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(adapter.remote.calls, 0);
}
TEST_F(AnnotationsTest, EpochChangeCannotAcknowledgeOrApply) {
  adapter.remote.revoke = &adapter.account;
  EXPECT_EQ(step(), SyncResult::Uncertain);
  EXPECT_EQ(read().acknowledged, 3);
  EXPECT_EQ(adapter.workspace.applications, 0);
  adapter.remote.revoke = nullptr;
  adapter.signIn();
  EXPECT_EQ(step(), SyncResult::Advanced);
}
TEST_F(AnnotationsTest, OversizedOrUnverifiedCaptureNeverSends) {
  adapter.workspace.count = 17;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  adapter.workspace.count = 1;
  adapter.workspace.rows[0].pendingVerification = true;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(adapter.remote.calls, 0);
}
TEST_F(AnnotationsTest, PersistenceFailureCannotAcknowledge) {
  adapter.workspace.fail = true;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(read().acknowledged, 3);
}
TEST(AnnotationMerge, NoteClearEmptyTextAndBookmarkTitle) {
  AnnotationRow slot, change;
  slot.text = "preserved passage";
  slot.note = "old note";
  change.start = "/p1";
  change.format = "xpointer";
  EXPECT_TRUE(LifecycleAnnotations::merge(AnnotationKind::Highlight, slot, change, true));
  EXPECT_EQ(slot.text, "preserved passage");
  EXPECT_TRUE(slot.note.empty());
  EXPECT_TRUE(slot.pendingVerification);
  change.title = "Bookmark title";
  EXPECT_TRUE(LifecycleAnnotations::merge(AnnotationKind::Bookmark, slot, change, false));
  EXPECT_EQ(slot.note, "Bookmark title");
  EXPECT_TRUE(slot.drawer.empty());
  EXPECT_FALSE(slot.pendingVerification);
}
TEST(AnnotationMerge, NewPageHighlightsFailAndExistingOnesRemainVerifiable) {
  AnnotationRow slot, change;
  change.start = "12";
  change.format = "page";
  EXPECT_FALSE(LifecycleAnnotations::merge(AnnotationKind::Highlight, slot, change, false));
  EXPECT_TRUE(LifecycleAnnotations::merge(AnnotationKind::Highlight, slot, change, true));
}

TEST_F(AnnotationsTest, EveryLocalPublicationCutRestartsWithoutLosingRemotePending) {
  adapter.remote.remoteCount = 3;
  const auto seed = card.disk;
  size_t operations = 0;
  {
    SyncOwner owner(journal, adapter, "adapter:one", "account:one", scratch.data(), scratch.size());
    ASSERT_EQ(owner.activate(), SyncResult::Ready);
    card.events.clear();
    ASSERT_EQ(owner.step(), SyncResult::Uncertain);
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
      adapter.remote.committed[0] = adapter.remote.committed[1] = false;
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
      for (unsigned n = 0; n < 4 && read().acknowledged == 3; ++n) step();
      EXPECT_EQ(read().acknowledged, 7);
      EXPECT_EQ(adapter.remote.commits, 2);
    }
}
TEST_F(AnnotationsTest, PagingAndMoreCannotAcknowledgeEarly) {
  adapter.workspace.last = false;
  EXPECT_EQ(step(), SyncResult::Uncertain);
  EXPECT_EQ(read().acknowledged, 3);
  EXPECT_EQ(adapter.workspace.progress[0].cursor, 1);
  card.restart();
  adapter.workspace.last = true;
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(adapter.workspace.progress[0].cursor, 2);
}
TEST_F(AnnotationsTest, AddEditAndTombstoneReceiptsAreNeverVerifiedEarly) {
  adapter.remote.remoteCount = 3;
  EXPECT_EQ(step(), SyncResult::Uncertain);
  EXPECT_EQ(adapter.remote.acks, 0);
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(adapter.remote.acks, 2);
}
TEST(AnnotationMerge, IdentityUsesServerIdRawLocatorOrProtocolKey) {
  AnnotationRow slot, change;
  slot.serverId = 1;
  change.serverId = 1;
  EXPECT_TRUE(LifecycleAnnotations::matches(slot, change));
  change.serverId = 2;
  EXPECT_FALSE(LifecycleAnnotations::matches(slot, change));
  slot.datetime = change.datetime = "2026-11-27 10:00:00";
  slot.start = change.start = "/raw/p1";
  EXPECT_TRUE(LifecycleAnnotations::matches(slot, change));
  change.start = "/raw/p2";
  EXPECT_FALSE(LifecycleAnnotations::matches(slot, change));
  slot.key = change.key = hash;
  EXPECT_TRUE(LifecycleAnnotations::matches(slot, change));
}

TEST_F(AnnotationsTest, StageFailureNeverAppliesOrAcknowledgesRemoteChanges) {
  adapter.remote.remoteCount = 3;
  adapter.workspace.failStage = true;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(adapter.remote.acks, 0);
  EXPECT_EQ(adapter.workspace.applications, 0);
  EXPECT_EQ(read().acknowledged, 3);
  card.restart();
  adapter.workspace.failStage = false;
  EXPECT_EQ(step(), SyncResult::Uncertain);
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(adapter.remote.commits, 2);
}
TEST_F(AnnotationsTest, ApplyFailureRetainsInboxAndNeverAcknowledges) {
  adapter.remote.remoteCount = 3;
  EXPECT_EQ(step(), SyncResult::Uncertain);
  adapter.workspace.failApply = true;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(adapter.remote.acks, 0);
  EXPECT_EQ(read().acknowledged, 3);
  card.restart();
  adapter.workspace.failApply = false;
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(adapter.remote.calls, 2);
}
TEST_F(AnnotationsTest, AckedInboxPublicationFailureRepeatsAckWithoutExchange) {
  adapter.remote.remoteCount = 3;
  EXPECT_EQ(step(), SyncResult::Uncertain);
  adapter.workspace.failFinish = true;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(read().acknowledged, 3);
  EXPECT_EQ(adapter.remote.acks, 1);
  card.restart();
  adapter.workspace.failFinish = false;
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(adapter.remote.calls, 2);
  EXPECT_EQ(adapter.remote.acks, 3);
}
TEST_F(AnnotationsTest, IncompatibleMatchSkipsWithoutOpeningWorkspaceOrSending) {
  matched[84] = 0;
  ASSERT_EQ(journal.checkpoint(1, matched), JournalResult::Ok);
  adapter.workspace.fail = true;
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(adapter.remote.calls, 0);
}
TEST_F(AnnotationsTest, InvalidRemoteRowsRemainPendingWithoutSidecarChanges) {
  adapter.remote.remoteCount = 3;
  adapter.workspace.remoteRows[0].row.serverId = 0;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(adapter.workspace.applications, 0);
  EXPECT_EQ(adapter.remote.acks, 0);
  EXPECT_EQ(read().acknowledged, 3);
}
TEST_F(AnnotationsTest, KnownMarkerFailureCannotFinishPhase) {
  adapter.workspace.failKnown = true;
  EXPECT_EQ(step(), SyncResult::Unavailable);
  EXPECT_EQ(read().acknowledged, 3);
  card.restart();
  adapter.workspace.failKnown = false;
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(adapter.remote.calls, 2);
}

TEST(AnnotationMerge, MissingOptionalFieldsPreserveNativeValuesAndExplicitEmptyTitleClears) {
  AnnotationRow slot, change;
  slot.chapter = "keep chapter";
  slot.color = "keep color";
  slot.end = "/p3";
  slot.page = 8;
  change.start = "/p1";
  change.format = "xpointer";
  change.fields = Start | Format;
  EXPECT_TRUE(LifecycleAnnotations::merge(AnnotationKind::Highlight, slot, change, true));
  EXPECT_EQ(slot.chapter, "keep chapter");
  EXPECT_EQ(slot.color, "keep color");
  EXPECT_EQ(slot.end, "/p3");
  EXPECT_EQ(slot.page, 8);
  change.note = "fallback";
  change.fields = Title;
  EXPECT_TRUE(LifecycleAnnotations::merge(AnnotationKind::Bookmark, slot, change, true));
  EXPECT_TRUE(slot.note.empty());
}

TEST_F(AnnotationsTest, StreamsServerPushDownWithoutAllocatingAResponseArray) {
  class Reader final : public AnnotationChangeReader {
   public:
    AnnotationChange current;
    const AnnotationChange* at(size_t i) override {
      if (i >= 100) return nullptr;
      current.op = AnnotationOp::Delete;
      current.row.serverId = i + 1;
      return &current;
    }
  } reader;
  adapter.workspace.remoteReader = &reader;
  adapter.remote.remoteCount = 100;
  EXPECT_EQ(step(), SyncResult::Uncertain);
  EXPECT_EQ(step(), SyncResult::Advanced);
  EXPECT_EQ(adapter.remote.acks, 2);
}
TEST_F(AnnotationsTest, ServerBookmarkWithoutDatetimeAndHighlightEditWithoutLocatorAreAccepted) {
  adapter.remote.remoteCount = 1;
  auto& change = adapter.workspace.remoteRows[0];
  change.op = AnnotationOp::Edit;
  change.row.datetime = {};
  change.row.start = {};
  change.row.format = {};
  change.row.fields = Title;
  change.row.title = "Server bookmark";
  EXPECT_EQ(step(), SyncResult::Uncertain);
  EXPECT_EQ(step(), SyncResult::Advanced);
}
