#include "booksync/interface/StatsWidgets.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "booksync/interface/PersonalWidgets.h"
#include "booksync/services/DeviceIndex.h"
#include "booksync/services/LocalLibrary.h"
#include "booksync/services/OfflineLibrary.h"
#include "booksync/services/PersonalReading.h"
#include "booksync/services/ReadModel.h"
using namespace booksync;
using namespace booksync::storage;
namespace {
constexpr PublishPaths Paths{"/index", "/index.tmp", "/index.bak"};
class IndexTest : public testing::Test {
 protected:
  void SetUp() override { card = {}; }
  Index index{Paths, "/data-a", "/data-b"};
  IndexRecord record{};
  void row(unsigned n) {
    record = {};
    snprintf(record.path.data(), record.path.size(), "/books/%05u.epub", n);
    snprintf(record.title.data(), record.title.size(), "Synthetic %05u", n);
    record.source.book.bytes[0] = 1;
    record.source.edition.bytes[0] = 2;
    record.source.revision.bytes[0] = 3;
  }
  void publish(unsigned count, uint32_t generation = 1) {
    ASSERT_TRUE(index.begin({generation, 1, 7, 11}));
    for (unsigned n = 0; n < count; ++n) {
      row(n);
      ASSERT_TRUE(index.append(record));
    }
    ASSERT_TRUE(index.commit());
  }
};
TEST_F(IndexTest, TenThousandBooksAndWarmWindowNeverLoadCatalog) {
  publish(10000);
  Index warm{Paths, "/data-a", "/data-b"};
  ASSERT_TRUE(warm.recover());
  EXPECT_EQ(warm.count(), 10000u);
  card.events.clear();
  ReadModel model{warm};
  ASSERT_TRUE(model.materialize({9980, 24, 16384}));
  const auto before = card.events.size();
  auto value = model.window();
  EXPECT_EQ(value.count, 20);
  EXPECT_STREQ(value.rows[0].title.data(), "Synthetic 09980");
  EXPECT_EQ(value.provenance.generation, 1u);
  EXPECT_EQ(value.provenance.capture, 11u);
  for (int i = 0; i < 100; ++i) EXPECT_EQ(model.window().count, 20);
  EXPECT_EQ(card.events.size(), before);
  EXPECT_LE(value.bytes, 16384u);
  EXPECT_LE(sizeof(ReadModel), 16384u);
}
TEST_F(IndexTest, RejectBudgetBeforeAnyReadAndChargeFailures) {
  publish(1);
  ReadModel model{index};
  card.events.clear();
  EXPECT_FALSE(model.materialize({0, 25, 16384}));
  EXPECT_FALSE(model.materialize({0, 1, 1}));
  EXPECT_TRUE(card.events.empty());
  card.files["/data-a"][100] ^= 1;
  EXPECT_FALSE(model.materialize({0, 1, 16384}));
  EXPECT_EQ(model.window().availability, Availability::Unavailable);
  EXPECT_GT(model.window().bytes, 0u);
}
TEST_F(IndexTest, InterruptedGenerationKeepsPublishedSnapshot) {
  publish(2);
  ASSERT_TRUE(index.begin({2, 1, 7, 11}));
  row(8);
  ASSERT_TRUE(index.append(record));
  Index restarted{Paths, "/data-a", "/data-b"};
  ASSERT_TRUE(restarted.recover());
  EXPECT_EQ(restarted.provenance().generation, 1u);
  EXPECT_EQ(restarted.count(), 2u);
}
TEST_F(IndexTest, PublicationCutsRecoverCompleteGeneration) {
  publish(2);
  const auto seed = card;
  ASSERT_TRUE(index.begin({2, 1, 7, 12}));
  row(3);
  ASSERT_TRUE(index.append(record));
  card.events.clear();
  ASSERT_TRUE(index.commit());
  const auto steps = card.events.size();
  for (size_t cut = 0; cut < steps; ++cut) {
    card = seed;
    Index writing{Paths, "/data-a", "/data-b"};
    ASSERT_TRUE(writing.recover());
    ASSERT_TRUE(writing.begin({2, 1, 7, 12}));
    row(3);
    ASSERT_TRUE(writing.append(record));
    card.events.clear();
    card.stop = static_cast<int>(cut);
    try {
      writing.commit();
    } catch (PowerCut&) {
    }
    card.restart();
    Index recovered{Paths, "/data-a", "/data-b"};
    ASSERT_TRUE(recovered.recover()) << cut;
    EXPECT_TRUE(recovered.provenance().generation == 1 || recovered.provenance().generation == 2);
    EXPECT_EQ(recovered.count(), recovered.provenance().generation == 1 ? 2u : 1u);
  }
}
TEST_F(IndexTest, InvalidationAndSubscriptionDoNotRepaintOrRead) {
  publish(3);
  ReadModel model{index};
  ASSERT_TRUE(model.materialize({0, 2, 16384}));
  auto old = model.window();
  auto subscription = model.subscribe();
  card.events.clear();
  model.invalidate(Invalidation::Capture);
  EXPECT_TRUE(model.changed(subscription));
  EXPECT_FALSE(model.changed(subscription));
  EXPECT_TRUE(model.window().stale);
  EXPECT_EQ(old.count, 2);
  EXPECT_FALSE(old.stale);
  EXPECT_TRUE(card.events.empty());
  model.retire(subscription);
  model.invalidate(Invalidation::Files);
  EXPECT_FALSE(model.changed(subscription));
}
TEST_F(IndexTest, StaleGenerationAndAccountCannotResolveOpen) {
  publish(2);
  ReadModel model{index};
  ASSERT_TRUE(model.materialize({0, 2, 16384}));
  auto key = model.window().rows[0].key;
  ASSERT_TRUE(index.read(key, record));
  EXPECT_STREQ(record.path.data(), "/books/00000.epub");
  publish(1, 2);
  EXPECT_FALSE(index.read(key, record));
  EXPECT_FALSE(index.begin({3, 2, 7, 11}));
}
TEST_F(IndexTest, FailedOrOversizedScanDoesNotPublishEmptySuccess) {
  publish(1);
  ASSERT_TRUE(index.begin({2, 1, 7, 11}));
  row(1);
  record.path.fill('x');
  EXPECT_FALSE(index.append(record));
  EXPECT_FALSE(index.commit());
  Index warm{Paths, "/data-a", "/data-b"};
  ASSERT_TRUE(warm.recover());
  EXPECT_EQ(warm.count(), 1u);
}
}  // namespace
namespace {
TEST_F(IndexTest, WorkerColdAndWarmScansBoundEveryTick) {
  ReadModel model{index};
  DeviceIndex worker{index, model};
  struct Synthetic {
    unsigned n = 0;
  } state;
  IndexSource source{[](void* p, IndexRecord& r) {
                       auto& n = static_cast<Synthetic*>(p)->n;
                       if (n == 10000) return ScanStep::End;
                       r = {};
                       snprintf(r.path.data(), r.path.size(), "/books/%05u.epub", n);
                       snprintf(r.title.data(), r.title.size(), "Synthetic %05u", n++);
                       return ScanStep::Record;
                     },
                     &state};
  for (unsigned generation = 1; generation <= 2; ++generation) {
    state.n = 0;
    ASSERT_TRUE(worker.start(source, {generation, 1, 7, 11}));
    unsigned ticks = 0;
    while (worker.busy()) {
      ASSERT_TRUE(worker.tick());
      EXPECT_LE(worker.lastWork(), 16);
      ++ticks;
    }
    EXPECT_EQ(ticks, 626u);
    EXPECT_EQ(index.count(), 10000u);
    EXPECT_TRUE(model.materialize({0, 24, 16384}));
    EXPECT_EQ(model.window().count, 24);
  }
}
TEST_F(IndexTest, FullOrReadOnlyCardPreservesCurrent) {
  publish(2);
  for (bool readonly : {false, true}) {
    card.readOnly = readonly;
    card.full = !readonly;
    EXPECT_FALSE(index.begin({2, 1, 7, 11}));
    card.readOnly = false;
    card.full = false;
    Index warm{Paths, "/data-a", "/data-b"};
    ASSERT_TRUE(warm.recover());
    EXPECT_EQ(warm.count(), 2u);
  }
}
TEST_F(IndexTest, MissingDataSelectsBackupWithoutMutation) {
  publish(2);
  publish(3, 2);
  card.files.erase("/data-b");
  Index warm{Paths, "/data-a", "/data-b"};
  ASSERT_TRUE(warm.recover());
  EXPECT_EQ(warm.count(), 2u);
  EXPECT_EQ(warm.provenance().generation, 1u);
}
}  // namespace
namespace {
TEST_F(IndexTest, FiftyThousandHistoryVisitsUse197BoundedWindowsAndPublishOnce) {
  publish(10000);
  ReadModel model{index};
  ASSERT_TRUE(model.materialize({0, 24, 16384}));
  auto subscription = model.subscribe();
  struct History {
    unsigned n = 0;
    char path[64]{};
  } history;
  HistorySource source{[](void* p, ReadingVisit& v) {
                         auto& h = *static_cast<History*>(p);
                         if (h.n == 50000) return HistoryStep::End;
                         snprintf(h.path, sizeof(h.path), "/books/%05u.epub", h.n % 10000);
                         v.source.book.bytes[0] = 1;
                         v.source.edition.bytes[0] = 2;
                         v.source.revision.bytes[0] = 3;
                         v.path = h.path;
                         v.sample = {};
                         v.sample.page = h.n;
                         v.sample.pages = 50000;
                         v.sample.totalSeconds = h.n * 2;
                         v.revision = ++h.n;
                         return HistoryStep::Record;
                       },
                       &history};
  source.owner = 7;
  ASSERT_TRUE(model.beginHistory(source));
  unsigned windows = 0;
  while (model.historyBusy()) {
    ASSERT_TRUE(model.historyTick());
    EXPECT_LE(model.lastHistoryWork(), 256);
    ++windows;
    if (model.historyBusy()) EXPECT_FALSE(model.changed(subscription));
  }
  EXPECT_EQ(windows, 197u);  // 196 bounded data windows and a terminal empty cursor.
  EXPECT_TRUE(model.changed(subscription));
  EXPECT_EQ(model.window().provenance.capture, 50000u);
  EXPECT_EQ(model.window().rows[0].page, 40000u);
  EXPECT_EQ(model.window().rows[0].seconds, 80000u);
  EXPECT_TRUE(model.window().rows[0].readingKnown);
}
}  // namespace
namespace {
TEST_F(IndexTest, FreshWriterRecoversBeforeChoosingInactiveSlot) {
  publish(2);
  Index fresh{Paths, "/data-a", "/data-b"};
  ASSERT_TRUE(fresh.begin({2, 1, 7, 11}));
  row(9);
  ASSERT_TRUE(fresh.append(record));
  Index warm{Paths, "/data-a", "/data-b"};
  ASSERT_TRUE(warm.recover());
  EXPECT_EQ(warm.count(), 2u);
  EXPECT_EQ(warm.provenance().generation, 1u);
  ASSERT_TRUE(warm.read({warm.provenance(), 0}, record));
  EXPECT_STREQ(record.path.data(), "/books/00000.epub");
}
TEST_F(IndexTest, AccountInvalidationErasesRowsAndOldLifetimeStaysRetired) {
  publish(2);
  ReadModel old{index};
  ASSERT_TRUE(old.materialize({0, 2, 16384}));
  auto subscription = old.subscribe();
  BookRow current;
  current.title[0] = 'A';
  PersonalCard resolved;
  resolved.progress = {0, true};
  resolved.timeLeft = {3600, true};
  old.setContinue(current, true, &resolved);
  EXPECT_TRUE(old.window().resume.timeLeftKnown);
  old.invalidate(Invalidation::Account);
  EXPECT_FALSE(old.window().resume.supplied);
  EXPECT_FALSE(old.window().resume.progressKnown);
  EXPECT_FALSE(old.window().resume.timeLeftKnown);
  EXPECT_EQ(old.window().resume.timeLeftAmount, 0u);
  EXPECT_EQ(old.window().count, 0);
  EXPECT_EQ(old.window().rows[0].title[0], 0);
  ReadModel next{index};
  ASSERT_TRUE(next.materialize({0, 2, 16384}));
  EXPECT_FALSE(next.changed(subscription));
}
TEST_F(IndexTest, SourceErrorCannotPublishPartialOrEmptySuccess) {
  publish(2);
  ReadModel model{index};
  DeviceIndex worker{index, model};
  IndexSource source{[](void*, IndexRecord&) { return ScanStep::Error; }, nullptr};
  ASSERT_TRUE(worker.start(source, {2, 1, 7, 11}));
  EXPECT_FALSE(worker.tick());
  EXPECT_FALSE(worker.busy());
  EXPECT_EQ(index.provenance().generation, 1u);
  EXPECT_EQ(index.count(), 2u);
}
}  // namespace
namespace {
TEST_F(IndexTest, SamePathCannotOverlayDifferentEdition) {
  publish(1);
  ReadModel model{index};
  ASSERT_TRUE(model.materialize({0, 1, 16384}));
  struct Input {
    unsigned n = 0;
  } state;
  HistorySource source{[](void* p, ReadingVisit& visit) {
                         auto& input = *static_cast<Input*>(p);
                         if (input.n++) return HistoryStep::End;
                         visit.path = "/books/00000.epub";
                         visit.sample.totalSeconds = 123;
                         visit.source.book.bytes[0] = 1;
                         visit.source.edition.bytes[0] = 9;
                         visit.source.revision.bytes[0] = 3;
                         visit.revision = 1;
                         return HistoryStep::Record;
                       },
                       &state};
  source.owner = 7;
  ASSERT_TRUE(model.beginHistory(source));
  while (model.historyBusy()) ASSERT_TRUE(model.historyTick());
  EXPECT_FALSE(model.window().rows[0].readingKnown);
}
}  // namespace
namespace {
TEST_F(IndexTest, RealLibraryPaintIsIoFreeAndOpenResolvesInWorker) {
  Index runtime{{"/.booksync/device.index", "/.booksync/device.index.tmp", "/.booksync/device.index.bak"},
                "/.booksync/device.a",
                "/.booksync/device.b"};
  ASSERT_TRUE(runtime.begin({1, 1, 0, 0}));
  row(0);
  ASSERT_TRUE(runtime.append(record));
  ASSERT_TRUE(runtime.commit());
  card.files[record.path.data()] = "synthetic book";
  enterLocalLibrary();
  auto& library = offlineLibrary();
  workLocalLibrary();
  EXPECT_EQ(library.window().windowCount, 0);
  EXPECT_EQ(library.window().publication, 0u);
  ASSERT_TRUE(library.requestWindow({0, 1, 16384}));
  workLocalLibrary();
  const auto window = library.window();
  ASSERT_EQ(window.count, 1);
  card.events.clear();
  for (unsigned paint = 0; paint < 100; ++paint) {
    EXPECT_EQ(library.window().count, 1);
    EXPECT_EQ(library.snapshot().generation, 1u);
  }
  EXPECT_TRUE(card.events.empty());
  EXPECT_EQ(library.open(window.rows[0].key), CommandStatus::Accepted);
  EXPECT_TRUE(card.events.empty());
  std::string path;
  EXPECT_FALSE(takeIndexedOpenRequest(path));
  workLocalLibrary();
  EXPECT_TRUE(takeIndexedOpenRequest(path));
  EXPECT_EQ(path, "/books/00000.epub");
  EXPECT_FALSE(takeIndexedOpenRequest(path));
  leaveLocalLibrary();
  EXPECT_EQ(library.window().availability, Availability::NotSaved);
}
}  // namespace
namespace {
TEST_F(IndexTest, CrossAccountHistoryRejectedBeforeProviderRead) {
  publish(1);
  ReadModel model{index};
  ASSERT_TRUE(model.materialize({0, 1, 16384}));
  unsigned calls = 0;
  HistorySource source{[](void* p, ReadingVisit&) {
                         ++*static_cast<unsigned*>(p);
                         return HistoryStep::End;
                       },
                       &calls, 8, 1};
  card.events.clear();
  EXPECT_FALSE(model.beginHistory(source));
  EXPECT_EQ(calls, 0u);
  EXPECT_TRUE(card.events.empty());
}
}  // namespace
namespace {
TEST_F(IndexTest, BatchedWindowsShareOneRowByteBudgetAndGeneration) {
  publish(10000);
  ReadModel model{index};
  WindowBatchRequest batch;
  batch.count = 3;
  batch.windows[0] = {0, 1, 16384};
  batch.windows[1] = {100, 11, 16384};
  batch.windows[2] = {9980, 12, 16384};
  ASSERT_TRUE(model.materializeBatch(batch));
  auto value = model.window();
  EXPECT_EQ(value.windowCount, 3);
  EXPECT_EQ(value.count, 24);
  EXPECT_EQ(value.windows[1].first, 1);
  EXPECT_EQ(value.windows[2].first, 12);
  EXPECT_STREQ(value.rows[1].title.data(), "Synthetic 00100");
  EXPECT_STREQ(value.rows[12].title.data(), "Synthetic 09980");
  EXPECT_LE(value.bytes, 16384u);
  card.events.clear();
  batch.windows[2].limit = 13;
  EXPECT_FALSE(model.materializeBatch(batch));
  EXPECT_TRUE(card.events.empty());
  batch.windows[2].limit = 12;
  batch.windows[1].bytes = 1;
  EXPECT_FALSE(model.materializeBatch(batch));
  EXPECT_TRUE(card.events.empty());
  batch.count = 17;
  EXPECT_FALSE(model.materializeBatch(batch));
  EXPECT_TRUE(card.events.empty());
}
TEST_F(IndexTest, BatchErrorIsConfinedToItsWindow) {
  publish(3);
  card.files["/data-a"][100] ^= 1;
  ReadModel model{index};
  WindowBatchRequest batch;
  batch.count = 2;
  batch.windows[0] = {0, 1, 16384};
  batch.windows[1] = {1, 1, 16384};
  EXPECT_FALSE(model.materializeBatch(batch));
  const auto value = model.window();
  EXPECT_EQ(value.windows[0].availability, Availability::Unavailable);
  EXPECT_EQ(value.windows[1].availability, Availability::Ready);
  EXPECT_EQ(value.count, 1);
  EXPECT_STREQ(value.rows[0].title.data(), "Synthetic 00001");
}
}  // namespace
namespace {
TEST_F(IndexTest, ReusedSameLengthSlotCannotMasqueradeAsBackupGeneration) {
  publish(2);
  publish(2, 2);
  ASSERT_TRUE(index.begin({3, 1, 7, 11}));
  row(9);
  ASSERT_TRUE(index.append(record));
  ASSERT_TRUE(index.append(record));
  index.cancel();  // Complete length, unpublished replacement of the old backup slot.
  card.files["/index"][0] = 'x';
  Index restarted{Paths, "/data-a", "/data-b"};
  EXPECT_FALSE(restarted.recover());
  EXPECT_FALSE(restarted.ready());  // Never label generation-three data as generation one.
}
TEST_F(IndexTest, DamagedCurrentReselectsBackupBeforeReusingSlot) {
  publish(2);
  publish(3, 2);
  card.files["/data-b"].resize(1);
  ASSERT_TRUE(index.begin({3, 1, 7, 11}));
  row(9);
  ASSERT_TRUE(index.append(record));
  index.cancel();
  Index restarted{Paths, "/data-a", "/data-b"};
  ASSERT_TRUE(restarted.recover());
  EXPECT_EQ(restarted.provenance().generation, 1u);
  EXPECT_EQ(restarted.count(), 2u);
}
}  // namespace
namespace {
TEST_F(IndexTest, UncertainFinalBarrierReconcilesVisibleGenerationBeforeRetry) {
  publish(1);
  const auto seed = card;
  ASSERT_TRUE(index.begin({2, 1, 7, 12}));
  row(9);
  ASSERT_TRUE(index.append(record));
  card.events.clear();
  ASSERT_TRUE(index.commit());
  int last = -1;
  for (size_t i = 0; i < card.events.size(); ++i)
    if (card.events[i] == "fs-sync:before") last = static_cast<int>(i);
  ASSERT_GE(last, 0);
  card = seed;
  Index retry{Paths, "/data-a", "/data-b"};
  ASSERT_TRUE(retry.begin({2, 1, 7, 12}));
  row(9);
  ASSERT_TRUE(retry.append(record));
  card.events.clear();
  card.fail = last;
  EXPECT_FALSE(retry.commit());
  EXPECT_EQ(retry.provenance().generation, 2u);
  EXPECT_TRUE(retry.ready());
  card.fail = -1;
  EXPECT_TRUE(retry.begin({3, 1, 7, 12}));
  retry.cancel();
}
}  // namespace

TEST_F(IndexTest, WidgetBatchPreservesIndependentContinueMissingShelfAndLocalFailure) {
  publish(2);
  ReadModel model{index};
  BookRow recent;
  recent.title[0] = 'R';
  recent.readingKnown = true;
  recent.page = 42;
  recent.pages = 100;
  model.setContinue(recent, true);
  WindowBatchRequest batch;
  batch.count = 3;
  batch.windows[0] = {0, 1, 16384, WindowSource::Continue};
  batch.windows[1] = {0, 3, 16384, WindowSource::Recent};
  batch.windows[2] = {0, 1, 16384, WindowSource::OnDevice};
  ASSERT_TRUE(model.materializeBatch(batch));
  auto value = model.window();
  EXPECT_EQ(value.windows[0].availability, Availability::Ready);
  EXPECT_EQ(value.windows[0].count, 1);
  EXPECT_EQ(value.windows[1].availability, Availability::NotSaved);
  EXPECT_EQ(value.windows[1].count, 0);
  EXPECT_EQ(value.windows[2].total, 2);
  ASSERT_TRUE(model.beginHistory({[](void*, ReadingVisit&) { return HistoryStep::End; }, nullptr, 7, 1}));
  ASSERT_TRUE(model.historyTick());
  EXPECT_TRUE(model.window().rows[0].readingKnown);
  EXPECT_EQ(model.window().rows[0].page, 42u);
  card.files["/data-a"][100] ^= 1;
  EXPECT_FALSE(model.materializeBatch(batch));
  value = model.window();
  EXPECT_EQ(value.windows[0].availability, Availability::Ready);
  EXPECT_EQ(value.windows[0].count, 1);
  EXPECT_EQ(value.windows[1].availability, Availability::NotSaved);
  EXPECT_EQ(value.windows[2].availability, Availability::Unavailable);
}

TEST(LibraryUpgradeNotice, CachedReadsNeverCheckpointAndDismissalRunsOnlyOnWorker) {
  using namespace booksync;
  static unsigned writes;
  writes = 0;
  auto dismiss = []() {
    ++writes;
    publishUpgradeState(false, false, nullptr);
    return true;
  };
  publishUpgradeState(true, false, dismiss);
  for (unsigned paint = 0; paint < 5; ++paint) EXPECT_TRUE(offlineLibrary().snapshot().upgradeNoticePending);
  EXPECT_EQ(writes, 0u);
  EXPECT_EQ(offlineLibrary().submit(Intent::DismissUpgradeNotice), CommandStatus::Accepted);
  EXPECT_EQ(writes, 0u);
  workLocalLibrary();
  EXPECT_EQ(writes, 1u);
  EXPECT_FALSE(offlineLibrary().snapshot().upgradeNoticePending);
  workLocalLibrary();
  EXPECT_EQ(writes, 1u);
  publishUpgradeState(false, false, nullptr);
}

TEST_F(IndexTest, QuerySnapshotUsesLibrarySubscriptionAndDoesNoPaintIo) {
  Index runtime{{"/.booksync/device.index", "/.booksync/device.index.tmp", "/.booksync/device.index.bak"},
                "/.booksync/device.a",
                "/.booksync/device.b"};
  ASSERT_TRUE(runtime.begin({1, 1, 0, 0}));
  row(0);
  ASSERT_TRUE(runtime.append(record));
  ASSERT_TRUE(runtime.commit());
  enterLocalLibrary();
  auto& library = offlineLibrary();
  workLocalLibrary();
  const auto subscription = library.subscribe();
  QueryRequest request{};
  request.filter.where = QueryWhere::OnDevice;
  ASSERT_TRUE(library.requestQuery(request));
  workLocalLibrary();
  QueryResult result{};
  library.readQuery(result);
  EXPECT_EQ(result.availability, Availability::Ready);
  EXPECT_EQ(result.count, 1);
  EXPECT_TRUE(library.changed(subscription));
  card.events.clear();
  for (unsigned paint = 0; paint < 100; ++paint) library.readQuery(result);
  EXPECT_TRUE(card.events.empty());
  invalidateLocalCapture();
  library.readQuery(result);
  EXPECT_EQ(result.availability, Availability::Unavailable);
  leaveLocalLibrary();
  library.readQuery(result);
  EXPECT_EQ(result.availability, Availability::NotSaved);
}

TEST(ReadModel, StatsBatchChargesItsFixedPayloadAndRejectsUnknownKinds) {
  WindowBatchRequest batch;
  batch.count = 1;
  batch.rows = 1;
  batch.windows[0].limit = 1;
  const uint32_t metadata = sizeof(LibraryWindow) - sizeof(BookRow) * LibraryRows;
  batch.bytes = metadata + sizeof(BookRow);
  EXPECT_TRUE(ReadModel::accepts(batch));
  batch.statsWidgets = statsWidgetBit(StatsWidget::Week);
  EXPECT_FALSE(ReadModel::accepts(batch));
  batch.bytes += sizeof(StatsWidgetBatch);
  EXPECT_TRUE(ReadModel::accepts(batch));
  batch.statsWidgets = 1u << StatsWidgetCount;
  EXPECT_FALSE(ReadModel::accepts(batch));
}
TEST_F(IndexTest, PersonalBudgetCountsFourDiscoverRowsAndRejectsBeforeStorage) {
  WindowBatchRequest batch;
  batch.count = 1;
  batch.windows[0] = {0, 1, LibraryBytes, WindowSource::OnDevice};
  batch.personalWidgets = 8;
  batch.rows = 5;
  constexpr uint32_t metadata = sizeof(LibraryWindow) - sizeof(BookRow) * LibraryRows;
  batch.bytes = metadata + sizeof(BookRow) + personalWidgetBytes(8);
  EXPECT_TRUE(ReadModel::accepts(batch));
  batch.rows = 4;
  card.events.clear();
  ReadModel model(index);
  EXPECT_FALSE(model.materializeBatch(batch));
  EXPECT_TRUE(card.events.empty());
  batch.rows = 5;
  --batch.bytes;
  EXPECT_FALSE(model.materializeBatch(batch));
  EXPECT_TRUE(card.events.empty());
  batch.bytes = LibraryBytes;
  batch.personalWidgets = 16;
  EXPECT_FALSE(ReadModel::accepts(batch));
  batch.personalWidgets = 8;
  batch.personalPick = 1000000001;
  EXPECT_FALSE(ReadModel::accepts(batch));
}
TEST_F(IndexTest, NewPersonalRequestsStayOffOnX3) {
  leaveLocalLibrary();
  enterLocalLibrary();
  WindowBatchRequest batch;
  batch.count = 1;
  batch.windows[0] = {0, 1, LibraryBytes, WindowSource::OnDevice};
  batch.personalWidgets = 1;
  EXPECT_FALSE(offlineLibrary().requestBatch(batch));
  leaveLocalLibrary();
}

TEST(ReadModel, CombinedStatsAndPersonalChargeBothConcretePayloads) {
  WindowBatchRequest batch;
  batch.count = 1;
  batch.windows[0].limit = 1;
  batch.rows = 2;
  batch.statsWidgets = statsWidgetBit(StatsWidget::Week);
  batch.personalWidgets = 1;
  constexpr uint32_t metadata = sizeof(LibraryWindow) - sizeof(BookRow) * LibraryRows;
  batch.bytes = metadata + sizeof(BookRow) + sizeof(StatsWidgetBatch) + personalWidgetBytes(1);
  EXPECT_TRUE(ReadModel::accepts(batch));
  --batch.bytes;
  EXPECT_FALSE(ReadModel::accepts(batch));
  ++batch.bytes;
  batch.rows = 1;
  EXPECT_FALSE(ReadModel::accepts(batch));
}

TEST(ContinueBudget, SummaryAndContextAreChargedToCaller) {
  booksync::WindowBatchRequest request;
  request.count = 1;
  auto& window = request.windows[0];
  window.source = booksync::WindowSource::Continue;
  window.limit = 1;
  window.bytes = sizeof(booksync::WindowSpan) + sizeof(booksync::BookRow);
  EXPECT_FALSE(booksync::ReadModel::accepts(request));
  window.bytes += sizeof(booksync::ContinueValues);
  EXPECT_TRUE(booksync::ReadModel::accepts(request));
}
