// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "booksync/ui/Series.h"
using namespace booksync;
namespace {
struct FakeSeriesLibrary : Library {
  Snapshot snap{Availability::Ready, 2, 3};
  DeliveryTicket owner{7, 3};
  QueryResult rows{};
  SeriesPlanView plan{};
  SeriesFlowView value{};
  std::vector<SeriesAction> actions;
  SeriesFlowRequest last{};
  unsigned polls = 0, opens = 0;
  bool online = true, accept = true, immediate = true, signedIn = true;
  Capabilities capabilities() const override { return {true, true, true}; }
  Snapshot snapshot() const override { return snap; }
  AccountView accountView() const override {
    AccountView v;
    v.ticket = owner;
    v.available = v.accountSupported = true;
    v.signedIn = signedIn;
    return v;
  }
  CommandStatus submit(Intent) override { return CommandStatus::Accepted; }
  bool requestQuery(const QueryRequest& r) override {
    rows.clear();
    rows.requestId = r.requestId;
    rows.publication = r.requestId;
    rows.provenance = {2, 3, 7, 1};
    rows.availability = Availability::Ready;
    rows.count = 2;
    rows.entries[0].book.key = {rows.provenance, 0};
    rows.hasMore = r.offset == 0;
    return true;
  }
  void readQuery(QueryResult& out) const override { out = rows; }
  bool requestSeries(SeriesRequest r) override {
    plan.clear();
    plan.requestId = r.requestId;
    plan.availability = Availability::Ready;
    plan.total = 9;
    plan.read = 2;
    return true;
  }
  void readSeries(SeriesPlanView& out) const override { out = plan; }
  void readSeriesFlow(SeriesFlowView& out) const override { out = value; }
  CommandStatus open(BookKey) override {
    ++opens;
    return CommandStatus::Accepted;
  }
  void preview() {
    const auto previousRevision = value.revision;
    value.clear();
    value.owner = owner;
    value.flow = 11;
    value.requestId = last.series.requestId;
    value.revision = previousRevision + 1;
    value.scene = SeriesFlowScene::Preview;
    value.online = online;
    value.plan.count = last.series.count;
    value.plan.requested = last.series.count;
    value.plan.token = 42;
    value.plan.earlierKnown = true;
    value.plan.earlier.book.key = {{2, 3, 7, 1}, 1};
  }
  CommandStatus seriesAction(const SeriesFlowRequest& r) override {
    actions.push_back(r.action);
    last = r;
    if (!accept) return CommandStatus::Unavailable;
    if (!immediate) return CommandStatus::Accepted;
    if (r.action == SeriesAction::Begin || r.action == SeriesAction::Preview) {
      preview();
      return CommandStatus::Accepted;
    }
    ++value.revision;
    if (r.action == SeriesAction::ReviewFiles || r.action == SeriesAction::ConnectAndReview) {
      value.scene = SeriesFlowScene::Files;
      value.fileCount = 2;
      value.files[0].choice = 21;
      value.files[1].choice = 22;
    }
    if (r.action == SeriesAction::ChooseFile) value.scene = SeriesFlowScene::Review;
    if (r.action == SeriesAction::Start) {
      value.scene = SeriesFlowScene::Queue;
      value.queue.id = 91;
      value.queue.count = 3;
      value.queue.items[0].cancelable = true;
      value.queue.items[1].cancelable = true;
      value.queue.items[2].cancelable = true;
    }
    if (r.action == SeriesAction::Resume || r.action == SeriesAction::ConnectAndResume) value.queue.paused = false;
    if (r.action == SeriesAction::Status) ++polls;
    if (r.action == SeriesAction::CancelRow) value.queue.items[r.row].state = SeriesItemState::Canceled;
    if (r.action == SeriesAction::CancelRemaining) value.queue.complete = true;
    return CommandStatus::Accepted;
  }
};
struct SeriesUiTest : testing::Test {
  FakeSeriesLibrary library;
  Series ui{library};
  void SetUp() override {
    ASSERT_TRUE(ui.enterSeries("series-key"));
    ui.pollSeries();
  }
  void preview() {
    ui.downloadSeries();
    ASSERT_EQ(ui.scene(), SeriesScene::Preview);
  }
  void queue() {
    preview();
    ui.reviewSeries();
    ui.chooseSeriesFile(0);
    ui.startSeriesFiles();
    ASSERT_EQ(ui.scene(), SeriesScene::Queue);
  }
};
TEST_F(SeriesUiTest, PageUsesExactContextAndBackRetiresWithoutOpeningReader) {
  EXPECT_EQ(ui.members().count, 2);
  EXPECT_EQ(ui.plan().total, 9u);
  ui.backSeries();
  EXPECT_FALSE(ui.live());
  EXPECT_EQ(library.opens, 0u);
}
TEST_F(SeriesUiTest, MemberOpenUsesExistingLibraryRouteOnlyOnExplicitAction) {
  ui.openSeriesBook(0);
  EXPECT_EQ(library.opens, 1u);
  ui.openSeriesBook(7);
  EXPECT_EQ(library.opens, 1u);
}
TEST_F(SeriesUiTest, ThreeRowPagingKeepsSeriesContext) {
  ui.nextSeriesPage(1);
  EXPECT_EQ(ui.offset(), 3u);
  ui.pollSeries();
  ui.nextSeriesPage(-1);
  EXPECT_EQ(ui.offset(), 0u);
}
TEST_F(SeriesUiTest, DefaultNextThreeAndPreviewCancelMatchesAndroid) {
  preview();
  EXPECT_EQ(library.last.series.count, 3);
  ui.backSeries();
  EXPECT_EQ(ui.scene(), SeriesScene::Page);
  EXPECT_EQ(library.actions.back(), SeriesAction::Close);
}
TEST_F(SeriesUiTest, CountCancelReturnsToSeriesRatherThanPreview) {
  preview();
  ui.changeSeriesCount();
  ui.backSeries();
  EXPECT_EQ(ui.scene(), SeriesScene::Page);
}
TEST_F(SeriesUiTest, InvalidCountReviewIsNoOpInSameEditor) {
  preview();
  ui.changeSeriesCount();
  ui.eraseSeriesCount();
  ui.appendSeriesCount('1');
  ui.appendSeriesCount('1');
  const auto before = library.actions.size();
  EXPECT_FALSE(ui.countValid());
  ui.submitSeriesCount();
  EXPECT_EQ(ui.scene(), SeriesScene::Count);
  EXPECT_EQ(library.actions.size(), before);
  ui.eraseSeriesCount();
  ui.eraseSeriesCount();
  EXPECT_FALSE(ui.countValid());
  ui.submitSeriesCount();
  EXPECT_EQ(ui.scene(), SeriesScene::Count);
  ui.appendSeriesCount('0');
  EXPECT_FALSE(ui.countValid());
  ui.submitSeriesCount();
  EXPECT_EQ(ui.scene(), SeriesScene::Count);
}
TEST_F(SeriesUiTest, AllValidCountsOneThroughTenSubmitExactRequest) {
  for (unsigned n = 1; n <= 10; ++n) {
    preview();
    ui.changeSeriesCount();
    while (*ui.countText()) ui.eraseSeriesCount();
    if (n == 10) {
      ui.appendSeriesCount('1');
      ui.appendSeriesCount('0');
    } else
      ui.appendSeriesCount('0' + n);
    EXPECT_TRUE(ui.countValid());
    ui.submitSeriesCount();
    EXPECT_EQ(ui.scene(), SeriesScene::Preview);
    EXPECT_EQ(library.last.series.count, n);
    ui.backSeries();
  }
}
TEST_F(SeriesUiTest, CountInputRejectsNonDigitsAndStaysBounded) {
  preview();
  ui.changeSeriesCount();
  EXPECT_FALSE(ui.appendSeriesCount('-'));
  for (unsigned i = 0; i < 20; ++i) ui.appendSeriesCount('9');
  EXPECT_LE(std::strlen(ui.countText()), 7u);
  EXPECT_FALSE(ui.countValid());
  ui.submitSeriesCount();
  EXPECT_EQ(ui.scene(), SeriesScene::Count);
}
TEST_F(SeriesUiTest, ClassicCountStepsDoNotWrap) {
  preview();
  ui.changeSeriesCount();
  for (unsigned n = 0; n < 20; ++n) ui.stepSeriesCount(1);
  EXPECT_STREQ(ui.countText(), "10");
  for (unsigned n = 0; n < 20; ++n) ui.stepSeriesCount(-1);
  EXPECT_STREQ(ui.countText(), "1");
}
TEST_F(SeriesUiTest, OfflineReviewUsesExplicitConnectAction) {
  library.online = false;
  preview();
  ui.reviewSeries();
  EXPECT_EQ(library.last.action, SeriesAction::ConnectAndReview);
}
TEST_F(SeriesUiTest, ChoiceAndStartCarryExactScopedOpaqueIdsAndPreviewToken) {
  preview();
  ui.reviewSeries();
  ui.chooseSeriesFile(1);
  EXPECT_EQ(library.last.choice, 22u);
  EXPECT_EQ(library.last.token, 42u);
  ui.startSeriesFiles();
  EXPECT_EQ(library.last.action, SeriesAction::Start);
  EXPECT_EQ(library.last.flow, 11u);
  EXPECT_EQ(library.opens, 0u);
}
TEST_F(SeriesUiTest, ReviewBackRepreviewsAndDoesNotStart) {
  preview();
  ui.reviewSeries();
  ui.chooseSeriesFile(0);
  ui.backSeries();
  EXPECT_EQ(library.last.action, SeriesAction::Preview);
  EXPECT_EQ(ui.scene(), SeriesScene::Preview);
  EXPECT_EQ(library.opens, 0u);
}
TEST_F(SeriesUiTest, ExplicitEarlierGapDoesNotWrapImplicitly) {
  preview();
  ui.earlierSeriesGap();
  EXPECT_TRUE(library.last.series.startKnown);
  EXPECT_EQ(library.last.series.start.ordinal, 1u);
  EXPECT_EQ(library.last.action, SeriesAction::Preview);
}
TEST_F(SeriesUiTest, AllSixApprovedNoticeVariantsOnlyReturnToSeries) {
  for (auto notice : {SeriesNotice::ConnectFailed, SeriesNotice::FileUnavailable, SeriesNotice::PreviewError,
                      SeriesNotice::Incomplete, SeriesNotice::Unavailable, SeriesNotice::NoEligible}) {
    preview();
    library.value.scene = SeriesFlowScene::Notice;
    library.value.notice = notice;
    ++library.value.revision;
    ui.pollSeries();
    EXPECT_EQ(ui.scene(), SeriesScene::Notice);
    ui.backSeries();
    EXPECT_EQ(ui.scene(), SeriesScene::Page);
  }
}
TEST_F(SeriesUiTest, QueueCompletedCanceledRowsDisabledAndNoCompletionOpen) {
  queue();
  library.value.queue.items[0].state = SeriesItemState::Complete;
  library.value.queue.items[1].state = SeriesItemState::Canceled;
  ui.pollSeries();
  const auto before = library.actions.size();
  ui.cancelSeriesRow(0);
  ui.cancelSeriesRow(1);
  EXPECT_EQ(library.actions.size(), before);
  ui.cancelSeriesRow(2);
  EXPECT_EQ(library.last.row, 2);
  EXPECT_EQ(library.last.action, SeriesAction::CancelRow);
  library.value.queue.complete = true;
  ui.pollSeries();
  const auto polls = library.polls;
  ui.pollSeries();
  EXPECT_EQ(library.polls, polls);
  EXPECT_EQ(library.opens, 0u);
}
TEST_F(SeriesUiTest, QueueBackStopsPollingAndLateResultCannotReopen) {
  queue();
  ui.backSeries();
  const auto polls = library.polls;
  library.value.scene = SeriesFlowScene::Queue;
  ui.pollSeries();
  EXPECT_EQ(ui.scene(), SeriesScene::Page);
  EXPECT_EQ(library.polls, polls);
}
TEST_F(SeriesUiTest, ResumeUsesSameQueueForAuthPauseAndOfflineConnect) {
  queue();
  library.value.queue.paused = true;
  library.value.online = true;
  ui.pollSeries();
  ui.resumeSeriesQueue();
  EXPECT_EQ(library.last.action, SeriesAction::Resume);
  library.value.queue.paused = true;
  library.value.online = false;
  ui.pollSeries();
  ui.resumeSeriesQueue();
  EXPECT_EQ(library.last.action, SeriesAction::ConnectAndResume);
}
TEST_F(SeriesUiTest, CancelRemainingDoesNotOpenReader) {
  queue();
  ui.cancelSeriesRemaining();
  EXPECT_EQ(library.last.action, SeriesAction::CancelRemaining);
  EXPECT_EQ(library.opens, 0u);
}
TEST_F(SeriesUiTest, AccountOrCatalogChangeClosesAndRetiresOldFlow) {
  queue();
  ++library.snap.accountEpoch;
  ui.pollSeries();
  EXPECT_FALSE(ui.live());
  EXPECT_EQ(ui.members().count, 0);
  EXPECT_EQ(library.opens, 0u);
}
TEST_F(SeriesUiTest, StaleRevisionAndForeignFlowNeverReplaceCurrentSheet) {
  preview();
  library.value.flow = 99;
  ++library.value.revision;
  ui.pollSeries();
  EXPECT_EQ(ui.scene(), SeriesScene::Preview);
  EXPECT_EQ(library.opens, 0u);
}
TEST_F(SeriesUiTest, UnregisteredWorkerUsesExistingOneButtonNotice) {
  library.accept = false;
  ui.downloadSeries();
  EXPECT_EQ(ui.scene(), SeriesScene::Notice);
  EXPECT_EQ(ui.flow().notice, SeriesNotice::Unavailable);
  ui.backSeries();
  EXPECT_EQ(ui.scene(), SeriesScene::Page);
}
TEST_F(SeriesUiTest, AsyncBeginResponseCorrelatedAndBackDiscardsLateDelivery) {
  library.immediate = false;
  ui.downloadSeries();
  EXPECT_TRUE(ui.pending());
  ui.backSeries();
  library.preview();
  ui.pollSeries();
  EXPECT_EQ(ui.scene(), SeriesScene::Page);
}
TEST_F(SeriesUiTest, AsyncFollowupWaitsForNewRevisionAndCannotRepeatAction) {
  preview();
  library.immediate = false;
  ui.reviewSeries();
  EXPECT_TRUE(ui.pending());
  const auto before = library.actions.size();
  ui.reviewSeries();
  EXPECT_EQ(library.actions.size(), before);
  ui.pollSeries();
  EXPECT_TRUE(ui.pending());
  library.value.scene = SeriesFlowScene::Files;
  ++library.value.revision;
  ui.pollSeries();
  EXPECT_FALSE(ui.pending());
  EXPECT_EQ(ui.scene(), SeriesScene::Files);
}
TEST_F(SeriesUiTest, CountCancelRetainsPublishedPageFactsWhenPlannerIsReused) {
  ASSERT_EQ(ui.plan().total, 9u);
  preview();
  ui.changeSeriesCount();
  library.plan.requestId = library.last.series.requestId;
  library.plan.total = 0;
  ui.backSeries();
  ui.pollSeries();
  EXPECT_EQ(ui.scene(), SeriesScene::Page);
  EXPECT_EQ(ui.plan().total, 9u);
}
}  // namespace

TEST(SeriesUi, PausedSignInPresentationUsesCachedLibraryAccount) {
  FakeSeriesLibrary library;
  Series series(library);
  ASSERT_TRUE(series.enterSeries("synthetic-series"));
  series.pollSeries();
  EXPECT_FALSE(series.signInNeeded());
  library.signedIn = false;
  series.pollSeries();
  EXPECT_TRUE(series.signInNeeded());
  library.signedIn = true;
  EXPECT_TRUE(series.signInNeeded());
  series.pollSeries();
  EXPECT_FALSE(series.signInNeeded());
  series.leaveSeries();
  EXPECT_FALSE(series.signInNeeded());
}
