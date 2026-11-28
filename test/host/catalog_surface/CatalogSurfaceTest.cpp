#include <gtest/gtest.h>

#include "booksync/ui/Catalog.h"

namespace {
using namespace booksync;
class Fixture : public Library {
 public:
  Fixture() { summary.downloadLogin = true; }
  Snapshot owner{};
  CatalogSummary summary{};
  QueryResult result{};
  QueryRequest wanted{};
  CatalogSources sources{};
  DownloadChoices choices{};
  DownloadStatus job{};
  unsigned queries = 0, batches = 0, opens = 0, starts = 0, cancels = 0, connects = 0;
  mutable unsigned reads = 0;
  Capabilities capabilities() const override { return {}; }
  Snapshot snapshot() const override {
    ++reads;
    return owner;
  }
  CommandStatus submit(Intent) override { return CommandStatus::Accepted; }
  CatalogSummary catalogSummary() const override {
    ++reads;
    return summary;
  }
  bool requestQuery(const QueryRequest& q) override {
    ++queries;
    wanted = q;
    result.requestId = q.requestId;
    return true;
  }
  void readQuery(QueryResult& out) const override {
    ++reads;
    out = result;
  }
  bool requestBatch(WindowBatchRequest) override {
    ++batches;
    return true;
  }
  bool requestCatalogSources(CatalogSourceRequest) override { return true; }
  void readCatalogSources(CatalogSources& out) const override { out = sources; }
  void readDownloadChoices(BookKey, DownloadChoices& out) const override { out = choices; }
  DownloadStatus bookDownloadStatus(BookKey) const override { return job; }
  CommandStatus connectForDownload(BookKey) override {
    ++connects;
    return CommandStatus::Accepted;
  }
  CommandStatus download(BookKey, uint8_t) override {
    ++starts;
    return CommandStatus::Accepted;
  }
  CommandStatus open(BookKey) override {
    ++opens;
    return CommandStatus::Accepted;
  }
  CommandStatus cancelDownload(uint64_t) override {
    ++cancels;
    return CommandStatus::Accepted;
  }
  void ready(QueryTruth local = QueryTruth::Yes) {
    result.provenance.accountEpoch = owner.accountEpoch;
    result.requestId = wanted.requestId;
    result.availability = Availability::Ready;
    result.count = 1;
    result.bytes = sizeof(QueryEntry);
    result.matchedBooks = 3;
    result.entries[0].onDevice = local;
    result.entries[0].book.title[0] = 'A';
    result.entries[0].book.key.provenance.accountEpoch = owner.accountEpoch;
  }
};
TEST(CatalogSurface, EntryIsCachedAndNeverConnects) {
  Fixture f;
  CatalogSurface ui(f);
  ui.enterCatalog();
  EXPECT_EQ(ui.catalogPanel(), CatalogSurface::Panel::Dashboard);
  EXPECT_EQ(f.batches, 1u);
  EXPECT_EQ(f.connects, 0u);
  EXPECT_EQ(f.queries, 0u);
  const auto reads = f.reads;
  (void)ui.catalogSummaryValue();
  (void)ui.catalogResult();
  (void)ui.catalogControls();
  EXPECT_EQ(f.reads, reads);
}
TEST(CatalogSurface, LocalBookOpensWithoutDownload) {
  Fixture f;
  CatalogSurface ui(f);
  ui.enterCatalog();
  ui.browseCatalog(CatalogRoot::OnCard);
  f.ready();
  ui.pollCatalog();
  EXPECT_EQ(ui.activateCatalogRow(0), CommandStatus::Accepted);
  EXPECT_EQ(f.opens, 1u);
  EXPECT_EQ(f.starts, 0u);
}
TEST(CatalogSurface, OfflineCancelStartsNothing) {
  Fixture f;
  CatalogSurface ui(f);
  ui.enterCatalog();
  ui.browseCatalog(CatalogRoot::All);
  f.ready(QueryTruth::No);
  ui.pollCatalog();
  ui.activateCatalogRow(0);
  EXPECT_EQ(ui.catalogDialog(), CatalogSurface::Dialog::Offline);
  ui.backCatalog();
  EXPECT_EQ(f.starts, 0u);
  EXPECT_EQ(f.connects, 0u);
  EXPECT_EQ(ui.catalogPanel(), CatalogSurface::Panel::Books);
}
TEST(CatalogSurface, RetiredControllerNeverReadsOrOpens) {
  Fixture f;
  CatalogSurface ui(f);
  ui.enterCatalog();
  ui.retireCatalog();
  const auto reads = f.reads;
  EXPECT_FALSE(ui.pollCatalog());
  EXPECT_EQ(f.reads, reads);
  EXPECT_EQ(ui.activateCatalogRow(0), CommandStatus::Unavailable);
  EXPECT_EQ(f.opens, 0u);
}
}  // namespace

#include "SceneFixture.h"
namespace {
class CatalogApprovedScenes : public testing::TestWithParam<unsigned> {};
TEST_P(CatalogApprovedScenes, CachedStateAndNoImplicitConnection) {
  catalogFixture::SceneLibrary library;
  library.configureScene(GetParam());
  CatalogSurface ui(library);
  ui.enterCatalog();
  catalogFixture::arrange(ui, library);
  EXPECT_TRUE(ui.catalogLive());
  EXPECT_EQ(library.connects, 0u);
  const auto reads = library.reads;
  (void)ui.catalogPanel();
  (void)ui.catalogState();
  (void)ui.catalogSummaryValue();
  (void)ui.catalogResult();
  (void)ui.catalogCovers();
  (void)ui.catalogSourcesValue();
  EXPECT_EQ(library.reads, reads) << "paint accessors must not call Library";
  const auto scene = GetParam();
  if (scene < 6 && scene != 1) EXPECT_EQ(ui.catalogPanel(), CatalogSurface::Panel::Dashboard);
  if (scene == 6 || scene == 7 || scene == 15) EXPECT_EQ(ui.catalogPanel(), CatalogSurface::Panel::Groups);
  if (scene == 13) EXPECT_EQ(ui.catalogState(), CatalogSurface::State::Empty);
  if (scene == 14) EXPECT_EQ(ui.catalogState(), CatalogSurface::State::NotSaved);
  if (scene == 16) EXPECT_EQ(ui.catalogState(), CatalogSurface::State::Error);
  if (scene == 17) EXPECT_EQ(ui.catalogState(), CatalogSurface::State::Unsupported);
  if (scene == 18) EXPECT_EQ(ui.catalogDialog(), CatalogSurface::Dialog::Choices);
  if (scene == 19) EXPECT_EQ(ui.catalogDialog(), CatalogSurface::Dialog::Offline);
  if (scene == 20) EXPECT_TRUE(ui.catalogPreparing());
  if (scene >= 21 && scene <= 24) EXPECT_EQ(ui.catalogDialog(), CatalogSurface::Dialog::TransferInfo);
  if (scene == 26) EXPECT_EQ(ui.catalogDialog(), CatalogSurface::Dialog::Login);
  if (scene == 27) EXPECT_EQ(ui.catalogInlineError(), DownloadError::NoSpace);
  if (scene == 29) {
    EXPECT_EQ(ui.catalogDialog(), CatalogSurface::Dialog::None);
    EXPECT_EQ(ui.catalogResult().entries[1].onDevice, QueryTruth::Yes);
    EXPECT_EQ(library.opens, 0u);
  }
}
INSTANTIATE_TEST_SUITE_P(Approved, CatalogApprovedScenes, testing::Range(0u, 30u),
                         [](const testing::TestParamInfo<unsigned>& info) {
                           std::string name = catalogFixture::names[info.param];
                           for (char& c : name)
                             if (c == '-') c = '_';
                           return name;
                         });
TEST(CatalogStateMachine, CancelConnectAndAccountChangeDoNotResurrect) {
  catalogFixture::SceneLibrary f;
  f.configureScene(19);
  CatalogSurface ui(f);
  ui.enterCatalog();
  catalogFixture::arrange(ui, f);
  ui.backCatalog();
  EXPECT_EQ(f.connects, 0u);
  EXPECT_EQ(ui.catalogPanel(), CatalogSurface::Panel::Books);
  ui.activateCatalogRow(1);
  ui.catalogDialogAction(0);
  EXPECT_EQ(f.connects, 1u);
  ui.backCatalog();
  f.summary.online = true;
  ++f.summary.publication;
  ui.pollCatalog();
  EXPECT_EQ(ui.catalogDialog(), CatalogSurface::Dialog::None);
  EXPECT_EQ(f.starts, 0u);
  ++f.owner.accountEpoch;
  ui.pollCatalog();
  EXPECT_FALSE(ui.catalogLive());
  const auto reads = f.reads;
  ui.pollCatalog();
  EXPECT_EQ(f.reads, reads);
}
TEST(CatalogStateMachine, NestedBackRestoresScopeCursorAndFocus) {
  catalogFixture::SceneLibrary f;
  f.configureScene(12);
  CatalogSurface ui(f);
  ui.enterCatalog();
  catalogFixture::arrange(ui, f);
  EXPECT_EQ(ui.catalogOffset(), 5u);
  ui.backCatalog();
  ui.pollCatalog();
  EXPECT_EQ(ui.catalogPanel(), CatalogSurface::Panel::Sources);
  EXPECT_EQ(ui.catalogFocus(), 1u);
  ui.backCatalog();
  EXPECT_EQ(ui.catalogPanel(), CatalogSurface::Panel::Dashboard);
}
TEST(CatalogStateMachine, RetiredOpenIntentCannotOpenCompletedDownload) {
  catalogFixture::SceneLibrary f;
  f.configureScene(18);
  CatalogSurface ui(f);
  ui.enterCatalog();
  catalogFixture::arrange(ui, f);
  ui.chooseCatalogDownload(1);
  EXPECT_EQ(f.starts, 1u);
  ui.backCatalog();
  f.status.sequence = ++f.sequence;
  f.status.state = DownloadState::Complete;
  f.status.id = 8;
  ui.pollCatalog();
  EXPECT_EQ(f.opens, 0u);
}
TEST(CatalogStateMachine, ActiveVerifiedCompletionOpensOnce) {
  catalogFixture::SceneLibrary f;
  f.configureScene(18);
  CatalogSurface ui(f);
  ui.enterCatalog();
  catalogFixture::arrange(ui, f);
  ui.chooseCatalogDownload(1);
  f.status.sequence = ++f.sequence;
  f.status.state = DownloadState::Complete;
  f.status.id = 8;
  ui.pollCatalog();
  ui.pollCatalog();
  EXPECT_EQ(f.opens, 1u);
}
TEST(CatalogStateMachine, OversizeAndUnterminatedValuesDoNotBecomeActions) {
  Fixture f;
  CatalogSurface ui(f);
  ui.enterCatalog();
  ui.browseCatalog(CatalogRoot::All);
  f.ready();
  f.result.entries[0].book.title.fill('x');
  ui.pollCatalog();
  EXPECT_EQ(ui.catalogState(), CatalogSurface::State::Error);
  EXPECT_EQ(ui.activateCatalogRow(0), CommandStatus::Unavailable);
  ui.retireCatalog();
  ui.enterCatalog();
  ui.browseCatalog(CatalogRoot::All);
  f.ready();
  f.result.bytes = LibraryBytes + 1;
  ui.pollCatalog();
  EXPECT_EQ(ui.catalogState(), CatalogSurface::State::Error);
}
}  // namespace

namespace {
TEST(CatalogStateMachine, DashboardFileTruthDoesNotComeFromCoverIdentity) {
  catalogFixture::SceneLibrary f;
  CatalogSurface ui(f);
  ui.enterCatalog();
  ui.pollCatalog();
  EXPECT_EQ(ui.activateCatalogShelfBook(0), CommandStatus::Accepted);
  EXPECT_EQ(f.opens, 1u);
  f.window.fileAvailability[0] = FileAvailability::Unknown;
  ++f.sequence;
  ui.pollCatalog();
  EXPECT_EQ(ui.activateCatalogShelfBook(0), CommandStatus::Unavailable);
  EXPECT_TRUE(ui.catalogAvailabilityUnknown());
  EXPECT_EQ(f.opens, 1u);
  EXPECT_EQ(f.starts, 0u);
}
TEST(CatalogStateMachine, BusyAndMissingRemainDifferent) {
  Fixture f;
  CatalogSurface ui(f);
  ui.enterCatalog();
  ui.browseCatalog(CatalogRoot::All);
  f.result.pending = true;
  ui.pollCatalog();
  EXPECT_EQ(ui.catalogState(), CatalogSurface::State::Loading);
}
TEST(CatalogStateMachine, PreviousPageIsNotADeadFocusStop) {
  catalogFixture::SceneLibrary f;
  CatalogSurface ui(f);
  ui.enterCatalog();
  ui.browseCatalog(CatalogRoot::All);
  ui.pollCatalog();
  ui.selectCatalogControl(ui.catalogVisibleRows());
  ui.stepCatalog(1);
  EXPECT_EQ(ui.catalogFocus(), ui.catalogVisibleRows() + 2);
}
TEST(CatalogStateMachine, NestedQueryOwnerRestoresTheParentRequest) {
  catalogFixture::SceneLibrary f;
  CatalogSurface ui(f);
  ui.enterCatalog();
  ui.browseCatalog(CatalogRoot::All);
  ui.pollCatalog();
  const auto before = f.wanted.requestId;
  f.result.requestId = before + 100;
  ++f.sequence;
  ui.pollCatalog();
  EXPECT_GT(f.wanted.requestId, before);
  ui.pollCatalog();
  EXPECT_EQ(ui.catalogState(), CatalogSurface::State::Ready);
}
TEST(CatalogStateMachine, TransferDialogBackRestoresTheSelectedRow) {
  catalogFixture::SceneLibrary f;
  f.configureScene(19);
  CatalogSurface ui(f);
  ui.enterCatalog();
  catalogFixture::arrange(ui, f);
  EXPECT_EQ(ui.catalogFocus(), 0);
  ui.backCatalog();
  EXPECT_EQ(ui.catalogFocus(), 1);
}
}  // namespace

namespace {
TEST(CatalogStateMachine, ReadFailureKeepsLastGoodLocalRowsWithoutRemoteActions) {
  catalogFixture::SceneLibrary f;
  CatalogSurface ui(f);
  ui.enterCatalog();
  ui.browseCatalog(CatalogRoot::All);
  ui.pollCatalog();
  f.result.availability = Availability::Unavailable;
  f.result.count = 0;
  ++f.sequence;
  ui.pollCatalog();
  EXPECT_EQ(ui.catalogState(), CatalogSurface::State::Error);
  ASSERT_EQ(ui.catalogVisibleRows(), 5);
  EXPECT_EQ(ui.activateCatalogRow(0), CommandStatus::Accepted);
  EXPECT_EQ(f.opens, 1u);
  EXPECT_EQ(ui.activateCatalogRow(1), CommandStatus::Unavailable);
  EXPECT_EQ(f.starts, 0u);
}
}  // namespace

namespace {
TEST(CatalogStateMachine, CoverRequestsYieldAtFourAndX3RequestsNone) {
  catalogFixture::SceneLibrary f;
  CatalogSurface ui(f);
  ui.enterCatalog();
  ui.pollCatalog();
  EXPECT_EQ(f.coverRequests, 4u);
  ui.pollCatalog();
  EXPECT_EQ(f.coverRequests, 8u);
  ui.retireCatalog();
  f.coverRequests = 0;
  ui.enterCatalog(true);
  ui.pollCatalog();
  EXPECT_EQ(f.coverRequests, 0u);
}
TEST(CatalogStateMachine, GroupBookAndAllLinkHaveDifferentActions) {
  catalogFixture::SceneLibrary f;
  f.configureScene(6);
  CatalogSurface ui(f);
  ui.enterCatalog();
  catalogFixture::arrange(ui, f);
  ui.selectCatalogControl(0);
  EXPECT_EQ(ui.activateCatalog(), CommandStatus::Accepted);
  EXPECT_EQ(f.opens, 1u);
  ui.selectCatalogControl(ui.catalogVisibleRows() + 3);
  EXPECT_EQ(ui.activateCatalog(), CommandStatus::Accepted);
  ui.pollCatalog();
  EXPECT_EQ(ui.catalogPanel(), CatalogSurface::Panel::Books);
  EXPECT_EQ(f.wanted.filter.authors.count, 1u);
}
}  // namespace

namespace {
TEST(CatalogStateMachine, ForeignJobCompletionCannotUseTheOriginalOpenIntent) {
  catalogFixture::SceneLibrary f;
  f.configureScene(18);
  CatalogSurface ui(f);
  ui.enterCatalog();
  catalogFixture::arrange(ui, f);
  ui.chooseCatalogDownload(0);
  f.status.id = 8;
  f.status.sequence = 1;
  f.status.state = DownloadState::Transferring;
  ui.pollCatalog();
  f.status.id = 9;
  f.status.sequence = 2;
  f.status.state = DownloadState::Complete;
  ui.pollCatalog();
  EXPECT_EQ(f.opens, 0u);
}
TEST(CatalogStateMachine, ChunkProgressOnlyRedrawsCoarseSteps) {
  catalogFixture::SceneLibrary f;
  f.configureScene(21);
  CatalogSurface ui(f);
  ui.enterCatalog();
  catalogFixture::arrange(ui, f);
  ++f.status.sequence;
  ++f.status.received;
  EXPECT_FALSE(ui.pollCatalog());
  ++f.status.sequence;
  f.status.received += 200000;
  EXPECT_TRUE(ui.pollCatalog());
}
}  // namespace

TEST(CatalogStateMachine, FailedShelfRetryStaysInLibraryAndRequiresExplicitInput) {
  catalogFixture::SceneLibrary library;
  library.configureScene(5);
  CatalogSurface ui(library);
  ui.enterCatalog();
  ui.pollCatalog();
  const auto batches = library.batches;
  ui.pollCatalog();
  EXPECT_EQ(library.batches, batches);
  ui.selectCatalogControl(0);
  EXPECT_EQ(ui.activateCatalog(), CommandStatus::Accepted);
  EXPECT_EQ(library.batches, batches + 1);
  EXPECT_EQ(ui.catalogPanel(), CatalogSurface::Panel::Dashboard);
  EXPECT_EQ(library.connects, 0u);
}

TEST(CatalogStateMachine, LibraryFailureKeepsSameAccountCachedLocalPreviews) {
  catalogFixture::SceneLibrary library;
  CatalogSurface ui(library);
  ui.enterCatalog();
  library.window.fileAvailability[1] = FileAvailability::ServerOnly;
  ui.pollCatalog();
  const auto first = ui.catalogCovers().rows[0].key;
  library.window.count = 0;
  for (unsigned n = 0; n < 2; ++n) {
    library.window.windows[n].availability = Availability::Unavailable;
    library.window.windows[n].count = 0;
    library.window.windows[n].first = 0;
    library.window.windows[n].bytes = sizeof(WindowSpan);
  }
  library.window.publication = ++library.sequence;
  ui.pollCatalog();
  EXPECT_EQ(ui.catalogState(), CatalogSurface::State::Error);
  ASSERT_EQ(ui.catalogCovers().count, 8);
  EXPECT_TRUE(ui.catalogCovers().rows[0].key == first);
  EXPECT_EQ(ui.activateCatalogShelfBook(0), CommandStatus::Accepted);
  EXPECT_EQ(library.opens, 1u);
  EXPECT_EQ(ui.activateCatalogShelfBook(1), CommandStatus::Unavailable);
  EXPECT_EQ(library.starts, 0u);
  ++library.owner.accountEpoch;
  ui.pollCatalog();
  EXPECT_FALSE(ui.catalogLive());
}

TEST(CatalogStateMachine, BrowseUsesPublishedCountsAndOmitsOnlyUnsupportedRoots) {
  catalogFixture::SceneLibrary library;
  library.summary.libraries.availability = Availability::Unsupported;
  library.summary.collections.availability = Availability::Unsupported;
  CatalogSurface ui(library);
  ui.enterCatalog();
  ui.pollCatalog();
  ui.browseCatalogIndex();
  ui.pollCatalog();
  ASSERT_EQ(ui.catalogControls(), 7);
  EXPECT_EQ(ui.catalogRootCount(CatalogRoot::All).value, 10020u);
  EXPECT_EQ(ui.catalogBrowseRootAt(4), CatalogRoot::Authors);
  EXPECT_EQ(ui.catalogBrowseRootAt(5), CatalogRoot::Series);
  EXPECT_EQ(ui.catalogBrowseRootAt(6), CatalogRoot::Lists);
  ui.selectCatalogControl(6);
  EXPECT_EQ(ui.activateCatalog(), CommandStatus::Accepted);
  EXPECT_EQ(ui.catalogRoot(), CatalogRoot::Lists);
}
