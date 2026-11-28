// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "booksync/services/SeriesFlow.h"
using namespace booksync;
namespace {
constexpr storage::PublishPaths journal{"/job", "/job.tmp", "/job.bak"};
constexpr DeliveryTicket owner{7, 1};
constexpr storage::ScopeView scope{"server-token", "account-token"};
struct SeriesFlowTest : testing::Test {
  uint64_t free = DownloadJobs::Headroom + 10000;
  unsigned links = 0;
  bool linkOk = true;
  storage::SourceIdentity linked{};
  Admission admission{{nullptr, [](void*) { return MemorySnapshot{128 * 1024, 64 * 1024, 0}; },
                       [](void*) { return true; }, [](void*) { return true; }, [](void*) { return true; }},
                      true};
  DownloadPorts ports{this, [](void* p) { return static_cast<SeriesFlowTest*>(p)->free; },
                      [](void* p, const DownloadSpec& selected) {
                        auto& f = *static_cast<SeriesFlowTest*>(p);
                        ++f.links;
                        f.linked = selected.identity;
                        return f.linkOk;
                      }};
  struct Source : DownloadSource {
    DownloadSpec selected{};
    uint64_t position = 0, requested = 0;
    unsigned opens = 0, reads = 0;
    bool wrongEdition = false, decline = false, badRange = false, badDigest = false, authorized = true, known = true;
    bool open(const DownloadSpec& s, uint64_t offset, const char*, DownloadReply& r) override {
      ++opens;
      selected = s;
      requested = offset;
      position = decline ? 0 : offset;
      r = {};
      r.identity = s.identity;
      if (wrongEdition) ++r.identity.edition.bytes[0];
      r.offset = badRange ? offset + 1 : position;
      r.bytes = 3;
      r.sizeKnown = known;
      std::strcpy(r.validator.data(), "strong-revision");
      return authorized;
    }
    int read(void* out, size_t) override {
      ++reads;
      if (position == 3) return 0;
      *static_cast<char*>(out) = badDigest ? 'x' : "abc"[position];
      ++position;
      return 1;
    }
    bool finish() override { return authorized; }
    void close() override {}
  } source;
  DownloadSpec spec{};
  bool online = true, writable = true, current = true;
  storage::SeriesQueueStore store{{"/queues", "/queues.tmp", "/queues.bak"}, "/series", scope};
  SeriesQueuePorts queuePorts{
      this, [](void* p) { return static_cast<SeriesFlowTest*>(p)->writable; },
      [](void* p) { return static_cast<SeriesFlowTest*>(p)->online; },
      [](void* p, const storage::SeriesQueueRecord&) { return static_cast<SeriesFlowTest*>(p)->current; }};
  storage::SeriesQueueRecord reviewed{};
  SeriesQueueView view{};
  void review(unsigned count = 3, const char* series = "series") {
    reviewed.clear();
    reviewed.token = 42;
    reviewed.count = count;
    reviewed.request.count = count;
    std::strcpy(reviewed.request.key.data(), series);
    for (unsigned i = 0; i < count; ++i) {
      auto& row = reviewed.books[i];
      row.file = spec;
      row.key.ordinal = i;
      row.file.identity.edition.bytes[0] = i + 1;
      std::snprintf(row.file.file.data(), row.file.file.size(), "file-%u", i);
      std::snprintf(row.file.path.data(), row.file.path.size(), "/books/book-%u.epub", i);
      std::snprintf(row.title.data(), row.title.size(), "Title %u", i);
    }
  }
  void run(SeriesQueue& q, unsigned turns = 100) {
    for (unsigned i = 0; i < turns; ++i) q.tick(owner);
  }

  uint8_t variants = 1;
  unsigned fileReads = 0;
  int unsupported = -1;
  bool filesOk = true, selectionOk = true, completeMetadata = true, connectOk = true;
  uint64_t catalogRevision = 1;
  SeriesConnect connection = SeriesConnect::Ready;
  QuerySource catalog{this,
                      [](void* p) {
                        auto& f = *static_cast<SeriesFlowTest*>(p);
                        return QueryState{
                            {2, 1, 7, f.catalogRevision}, 3, f.completeMetadata ? QueryAllFields : 0, true};
                      },
                      [](void*, BookKey key, QueryRecord& out) {
                        out.clear();
                        out.book.key = key;
                        std::snprintf(out.book.title.data(), out.book.title.size(), "Title %u", key.ordinal);
                        out.seriesCount = 1;
                        std::strcpy(out.series[0].data(), "Synthetic series");
                        std::strcpy(out.seriesKeys[0].data(), "series");
                        out.seriesPositionsKnown[0] = true;
                        out.seriesPositions[0] = key.ordinal + 1;
                        out.seriesRanks[0] = key.ordinal;
                        out.status = QueryShow::Unread;
                        out.onDevice = QueryTruth::No;
                        return true;
                      }};
  SeriesPlan planner{catalog, {this, [](void*) { return uint64_t(1); }, [](void*, BookKey) { return QueryTruth::No; }}};
  SeriesFlowPorts flowPorts{
      this,
      [](void* p) { return static_cast<SeriesFlowTest*>(p)->online; },
      [](void* p, BookKey key, SeriesFileSet& out) {
        auto& f = *static_cast<SeriesFlowTest*>(p);
        ++f.fileReads;
        out.clear();
        if (!f.filesOk) return false;
        out.complete = true;
        if (int(key.ordinal) == f.unsupported) return true;
        out.count = f.variants;
        for (uint8_t i = 0; i < out.count && i < SeriesFileChoices; ++i) {
          auto& row = out.files[i];
          row.selected = f.spec;
          row.selected.identity.edition.bytes[0] = key.ordinal + 1;
          std::snprintf(row.selected.path.data(), row.selected.path.size(), "/books/choice-%u-%u.epub", key.ordinal, i);
          std::snprintf(row.selected.file.data(), row.selected.file.size(), "file-%u", key.ordinal);
          std::snprintf(row.selected.variant.data(), row.selected.variant.size(), "variant-%u", i);
          std::strcpy(row.label.data(), i ? "Other EPUB" : "EPUB");
          std::strcpy(row.destination.data(), "Books folder");
        }
        return true;
      },
      [](void* p, BookKey, const DownloadSpec&) { return static_cast<SeriesFlowTest*>(p)->selectionOk; },
      [](void* p) { return static_cast<SeriesFlowTest*>(p)->connectOk; },
      [](void* p) {
        auto& f = *static_cast<SeriesFlowTest*>(p);
        if (f.connection == SeriesConnect::Ready) f.online = true;
        return f.connection;
      },
      [](void* p) { return static_cast<SeriesFlowTest*>(p)->writable; }};
  DownloadJobs engine{journal, admission, ports};
  SeriesQueue queue{store, engine, source, queuePorts, scope, owner, "/series-job"};
  SeriesFlow flow{planner, queue, flowPorts, owner};
  SeriesFlowView published{};
  SeriesFlowRequest intent{};
  bool send(SeriesAction action) {
    intent.owner = owner;
    intent.flow = published.flow;
    intent.token = published.plan.token;
    intent.review = published.reviewId;
    intent.action = action;
    return flow.action(intent);
  }
  void ticks(unsigned n = 40) {
    for (unsigned i = 0; i < n; ++i) flow.tick(owner);
    flow.copyFlow(published);
  }
  void begin() {
    intent.owner = owner;
    intent.action = SeriesAction::Begin;
    intent.series.requestId = 10;
    intent.series.count = 3;
    std::strcpy(intent.series.key.data(), "series");
    ASSERT_TRUE(flow.action(intent));
    ticks(4);
    ASSERT_EQ(published.scene, SeriesFlowScene::Preview);
    ASSERT_FALSE(published.pending);
  }
  void reviewFiles() {
    begin();
    ASSERT_TRUE(send(SeriesAction::ReviewFiles));
    ticks(6);
    ASSERT_EQ(published.scene, SeriesFlowScene::Review);
  }
  void SetUp() override {
    card = {};
    admission.setReading(false);
    std::strcpy(spec.server.data(), "server-token");
    std::strcpy(spec.account.data(), "account-token");
    std::strcpy(spec.book.data(), "opaque:book");
    std::strcpy(spec.file.data(), "opaque:file");
    std::strcpy(spec.variant.data(), "selected");
    std::strcpy(spec.path.data(), "/books/book.epub");
    spec.bytes = 3;
    spec.sizeKnown = true;
    spec.identity.book.bytes[0] = 1;
    spec.identity.edition.bytes[0] = 2;
    constexpr uint8_t digest[] = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
                                  0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
                                  0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
    std::memcpy(spec.identity.revision.bytes.data(), digest, 32);
    review();
    ASSERT_TRUE(queue.recover());
  }
  void complete(DownloadJobs& j) {
    for (unsigned i = 0; i < 20 && j.busy(); ++i) j.tick(owner);
  }
};

TEST_F(SeriesFlowTest, SingleFilesSkipChooserAndFinalReviewStartsExactFrozenFiles) {
  reviewFiles();
  EXPECT_EQ(source.opens, 0u);
  EXPECT_TRUE(published.review[0].supported);
  EXPECT_STREQ(published.review[0].destination.data(), "Books folder");
  ASSERT_TRUE(send(SeriesAction::Start));
  ticks(100);
  ASSERT_TRUE(send(SeriesAction::Status));
  ticks(2);
  EXPECT_EQ(published.scene, SeriesFlowScene::Queue);
  EXPECT_TRUE(published.queue.complete);
  EXPECT_EQ(source.opens, 3u);
  EXPECT_EQ(card.files["/books/choice-0-0.epub"], "abc");
}
TEST_F(SeriesFlowTest, MultipleOptionsRequireExactScopedChoiceForEachBook) {
  variants = 2;
  begin();
  ASSERT_TRUE(send(SeriesAction::ReviewFiles));
  ticks(1);
  EXPECT_EQ(published.scene, SeriesFlowScene::Files);
  EXPECT_EQ(source.opens, 0u);
  for (uint8_t i = 0; i < 3; ++i) {
    ASSERT_EQ(published.fileRow, i);
    intent.row = i;
    intent.choice = published.files[1].choice;
    ASSERT_TRUE(send(SeriesAction::ChooseFile));
    ticks(1);
  }
  EXPECT_EQ(published.scene, SeriesFlowScene::Review);
  ASSERT_TRUE(send(SeriesAction::Start));
  ticks(100);
  for (uint8_t i = 0; i < 3; ++i) {
    char path[80];
    std::snprintf(path, sizeof(path), "/books/choice-%u-1.epub", i);
    EXPECT_EQ(card.files[path], "abc");
  }
}
TEST_F(SeriesFlowTest, UnsupportedFileRemainsVisibleInReviewAndQueue) {
  unsupported = 1;
  reviewFiles();
  EXPECT_FALSE(published.review[1].supported);
  ASSERT_TRUE(send(SeriesAction::Start));
  ticks(100);
  ASSERT_TRUE(send(SeriesAction::Status));
  ticks(2);
  EXPECT_EQ(published.queue.items[1].state, SeriesItemState::Unsupported);
  EXPECT_EQ(source.opens, 2u);
}
TEST_F(SeriesFlowTest, MembershipChangeAtStartUsesExistingUnavailableNoticeWithoutJob) {
  reviewFiles();
  ++catalogRevision;
  ASSERT_TRUE(send(SeriesAction::Start));
  ticks();
  EXPECT_EQ(published.scene, SeriesFlowScene::Notice);
  EXPECT_EQ(published.notice, SeriesNotice::Unavailable);
  EXPECT_EQ(source.opens, 0u);
}
TEST_F(SeriesFlowTest, FrozenFileProofChangeAtStartDoesNotSubstituteFile) {
  reviewFiles();
  selectionOk = false;
  ASSERT_TRUE(send(SeriesAction::Start));
  ticks();
  EXPECT_EQ(published.scene, SeriesFlowScene::Notice);
  EXPECT_EQ(source.opens, 0u);
}
TEST_F(SeriesFlowTest, IncompleteMembershipUsesApprovedNotice) {
  completeMetadata = false;
  intent.owner = owner;
  intent.action = SeriesAction::Begin;
  intent.series.requestId = 10;
  std::strcpy(intent.series.key.data(), "series");
  ASSERT_TRUE(flow.action(intent));
  ticks();
  EXPECT_EQ(published.notice, SeriesNotice::Incomplete);
  EXPECT_EQ(published.scene, SeriesFlowScene::Notice);
}
TEST_F(SeriesFlowTest, FileOptionsUnavailableAndOverflowDoNotInventChoices) {
  begin();
  filesOk = false;
  ASSERT_TRUE(send(SeriesAction::ReviewFiles));
  ticks();
  EXPECT_EQ(published.notice, SeriesNotice::FileUnavailable);
  EXPECT_EQ(source.opens, 0u);
}
TEST_F(SeriesFlowTest, OfflineRequiresExplicitConnectAndFailureShowsOneNotice) {
  online = false;
  begin();
  EXPECT_FALSE(published.online);
  connectOk = false;
  ASSERT_TRUE(send(SeriesAction::ConnectAndReview));
  ticks();
  EXPECT_EQ(published.scene, SeriesFlowScene::Notice);
  EXPECT_EQ(published.notice, SeriesNotice::ConnectFailed);
  EXPECT_EQ(source.opens, 0u);
}
TEST_F(SeriesFlowTest, PendingConnectCannotStartJobsOrAcceptSecondAction) {
  online = false;
  connection = SeriesConnect::Pending;
  begin();
  ASSERT_TRUE(send(SeriesAction::ConnectAndReview));
  ticks();
  EXPECT_TRUE(published.pending);
  EXPECT_FALSE(send(SeriesAction::Start));
  EXPECT_EQ(source.opens, 0u);
  connection = SeriesConnect::Ready;
  ticks(8);
  EXPECT_EQ(published.scene, SeriesFlowScene::Review);
}
TEST_F(SeriesFlowTest, BackToPreviewDiscardsOldReviewAndChoiceIds) {
  variants = 2;
  begin();
  ASSERT_TRUE(send(SeriesAction::ReviewFiles));
  ticks(1);
  const auto oldChoice = published.files[0].choice, oldReview = published.reviewId;
  ASSERT_TRUE(send(SeriesAction::Preview));
  ticks(4);
  ASSERT_TRUE(send(SeriesAction::ReviewFiles));
  ticks(1);
  EXPECT_NE(published.files[0].choice, oldChoice);
  EXPECT_NE(published.reviewId, oldReview);
  intent.row = 0;
  intent.choice = oldChoice;
  EXPECT_FALSE(send(SeriesAction::ChooseFile));
  EXPECT_EQ(source.opens, 0u);
}
TEST_F(SeriesFlowTest, ClosedFlowRejectsLateActionAndNeverOpensReader) {
  reviewFiles();
  const auto late = intent;
  ASSERT_TRUE(send(SeriesAction::Close));
  ticks();
  EXPECT_EQ(published.flow, 0u);
  EXPECT_FALSE(flow.action(late));
  EXPECT_EQ(source.opens, 0u);
}
TEST_F(SeriesFlowTest, LeavingQueueDoesNotCancelAcceptedDownloads) {
  reviewFiles();
  ASSERT_TRUE(send(SeriesAction::Start));
  ticks(5);
  ASSERT_TRUE(send(SeriesAction::Close));
  ticks(100);
  EXPECT_EQ(source.opens, 3u);
  EXPECT_EQ(links, 3u);
  EXPECT_EQ(published.flow, 0u);
}
TEST_F(SeriesFlowTest, OwnerChangeClearsProjectionAndDrainsOldTransfer) {
  reviewFiles();
  ASSERT_TRUE(send(SeriesAction::Start));
  ticks(4);
  flow.tick({7, 2});
  flow.copyFlow(published);
  EXPECT_EQ(published.flow, 0u);
  EXPECT_FALSE(flow.action(intent));
}
TEST_F(SeriesFlowTest, QueueCompletionStaysInSameQueueSheet) {
  reviewFiles();
  ASSERT_TRUE(send(SeriesAction::Start));
  ticks(100);
  ASSERT_TRUE(send(SeriesAction::Status));
  ticks(2);
  EXPECT_TRUE(published.queue.complete);
  EXPECT_EQ(published.scene, SeriesFlowScene::Queue);
  ticks(50);
  EXPECT_EQ(published.scene, SeriesFlowScene::Queue);
}
TEST_F(SeriesFlowTest, ResumeConnectFailureKeepsSamePausedQueue) {
  reviewFiles();
  ASSERT_TRUE(send(SeriesAction::Start));
  ticks(3);
  online = false;
  ticks(50);
  ASSERT_TRUE(send(SeriesAction::Status));
  ticks(2);
  ASSERT_TRUE(published.queue.paused);
  const auto id = published.queue.id;
  connectOk = false;
  ASSERT_TRUE(send(SeriesAction::ConnectAndResume));
  ticks(2);
  EXPECT_EQ(published.scene, SeriesFlowScene::Queue);
  EXPECT_EQ(published.queue.id, id);
  EXPECT_TRUE(published.queue.paused);
}
TEST_F(SeriesFlowTest, BeginFindsExistingNonterminalQueueRatherThanStartingAgain) {
  reviewFiles();
  ASSERT_TRUE(send(SeriesAction::Start));
  ticks(4);
  online = false;
  ticks(50);
  ASSERT_TRUE(send(SeriesAction::Close));
  intent.action = SeriesAction::Begin;
  intent.flow = 0;
  ++intent.series.requestId;
  ASSERT_TRUE(flow.action(intent));
  ticks(4);
  EXPECT_EQ(published.scene, SeriesFlowScene::Queue);
  EXPECT_TRUE(published.queue.paused);
}
TEST_F(SeriesFlowTest, RevokedReadingGateDefersFileResolutionUntilOwnerPermitsWork) {
  begin();
  ASSERT_TRUE(send(SeriesAction::ReviewFiles));
  writable = false;
  ticks(50);
  EXPECT_EQ(fileReads, 0u);
  EXPECT_EQ(source.opens, 0u);
  EXPECT_TRUE(published.pending);
  writable = true;
  ticks(6);
  EXPECT_EQ(published.scene, SeriesFlowScene::Review);
  EXPECT_EQ(fileReads, 3u);
}
}  // namespace
