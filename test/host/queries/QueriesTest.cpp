// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include <cstring>

#include "booksync/services/Queries.h"
using namespace booksync;
namespace {
struct QueryFixture : testing::Test {
  QueryRecord record{};
  QueryResult result{};
  QueryRequest request{};
  uint32_t generation = 1, books = 80, reads = 0;
  QueryFields fields = QueryAllFields;
  bool fail = false;
  QuerySource source() {
    return {this,
            [](void* p) {
              auto& f = *static_cast<QueryFixture*>(p);
              return QueryState{{f.generation, 1, 7, 0}, f.books, f.fields, true};
            },
            [](void* p, BookKey key, QueryRecord& row) {
              auto& f = *static_cast<QueryFixture*>(p);
              ++f.reads;
              if (f.fail) return false;
              row = {};
              row.book.key = key;
              std::snprintf(row.book.title.data(), row.book.title.size(), "Book %03u", key.ordinal);
              std::strcpy(row.authors[0].data(), key.ordinal % 2 ? "Writer B" : "Writer A");
              row.authorCount = 1;
              std::strcpy(row.series[0].data(), "Primary");
              std::strcpy(row.series[1].data(), "Secondary");
              row.seriesCount = 2;
              std::strcpy(row.seriesKeys[0].data(), "series-primary");
              std::strcpy(row.seriesKeys[1].data(), "series-secondary");
              row.seriesPositions[1] = double(80 - key.ordinal) / 2;
              row.seriesPositionsKnown[1] = true;
              std::strcpy(row.tags[0].data(), "Saved");
              row.tagCount = 1;
              row.status = key.ordinal % 2 ? QueryShow::Rereading : QueryShow::Unread;
              row.onDevice = key.ordinal % 2 ? QueryTruth::No : QueryTruth::Yes;
              row.wantToRead = key.ordinal % 3 == 0;
              row.book.pages = 900;  // Native rendered pages intentionally differ from bibliographic length.
              row.lengthPages = 100 + key.ordinal;
              row.pagesKnown = row.ratingKnown = true;
              row.rating = 4;
              std::strcpy(row.added.data(), "2026-11-01");
              std::strcpy(row.scopes[0].data(), key.ordinal % 2 ? "scope-b" : "scope-a");
              row.scopeCount = 1;
              return true;
            }};
  }
  Queries query{source()};
  void run() {
    ASSERT_TRUE(query.start(request));
    unsigned ticks = 0;
    while (query.busy()) {
      query.tick();
      EXPECT_LE(query.lastWork(), Queries::Batch);
      ASSERT_LT(++ticks, 100000u);
    }
    query.copyResult(result);
  }
};
TEST_F(QueryFixture, SearchFiltersBeforePagingAndCountsWholeSelection) {
  std::strcpy(request.filter.text.data(), "BOOK writer");
  request.filter.where = QueryWhere::OnDevice;
  request.offset = 12;
  request.limit = 5;
  run();
  ASSERT_EQ(result.availability, Availability::Ready);
  EXPECT_EQ(result.matchedBooks, 40u);
  ASSERT_EQ(result.count, 5);
  EXPECT_EQ(result.entries[0].book.key.ordinal, 24u);
}
TEST_F(QueryFixture, SecondarySeriesAndScopedLists) {
  std::strcpy(request.filter.series.values[0].data(), "secondary");
  request.filter.series.count = 1;
  std::strcpy(request.scope.data(), "scope-b");
  run();
  ASSERT_EQ(result.availability, Availability::Ready);
  EXPECT_EQ(result.matchedBooks, 40u);
  request.filter.show = QueryShow::Reading;
  run();
  EXPECT_EQ(result.matchedBooks, 40u);
  request.filter.withoutSeries = true;
  run();
  EXPECT_EQ(result.matchedBooks, 0u);
}
TEST_F(QueryFixture, CountedFacetsCountEachBookOnceAndHonorScope) {
  request.kind = QueryKind::Facets;
  request.field = QueryFacet::Series;
  std::strcpy(request.scope.data(), "scope-a");
  run();
  ASSERT_EQ(result.availability, Availability::Ready);
  ASSERT_EQ(result.count, 2);
  EXPECT_STREQ(result.entries[1].label.data(), "Secondary");
  EXPECT_EQ(result.entries[1].books, 40u);
}
TEST_F(QueryFixture, GroupsUseFirstAuthorAndPrimarySeries) {
  request.kind = QueryKind::Groups;
  request.filter.group = QueryGroup::Author;
  run();
  ASSERT_EQ(result.count, 2);
  EXPECT_EQ(result.entries[0].books, 40u);
  request.filter.group = QueryGroup::Series;
  run();
  ASSERT_EQ(result.count, 1);
  EXPECT_STREQ(result.entries[0].label.data(), "Primary");
}
TEST_F(QueryFixture, MissingMetadataIsUnsupportedEvenForEmptyCatalog) {
  fields &= ~queryField(QueryField::Series);
  books = 0;
  request.filter.series.count = 1;
  std::strcpy(request.filter.series.values[0].data(), "Secondary");
  run();
  EXPECT_EQ(result.availability, Availability::Unsupported);
  EXPECT_EQ(reads, 0u);
  EXPECT_EQ(result.count, 0);
}
TEST_F(QueryFixture, GenerationChangeAndReadFailureDiscardPartialResults) {
  ASSERT_TRUE(query.start(request));
  query.tick();
  ++generation;
  while (query.busy()) query.tick();
  query.copyResult(result);
  EXPECT_EQ(result.availability, Availability::Unavailable);
  EXPECT_EQ(result.count, 0);
  fail = true;
  run();
  EXPECT_EQ(result.availability, Availability::Unavailable);
}
TEST_F(QueryFixture, InvalidBudgetsAndUnsupportedSortNeverReturnPlausibleEmpty) {
  request.limit = LibraryRows + 1;
  EXPECT_FALSE(query.start(request));
  request.limit = 12;
  request.filter.sort = QuerySort::Match;
  fields &= ~queryField(QueryField::Match);
  run();
  EXPECT_EQ(result.availability, Availability::Unsupported);
}
TEST_F(QueryFixture, NumericRangesWantToReadAndStableDescendingPages) {
  request.filter.pages = {120, 140, true, true};
  request.filter.wantToRead = true;
  request.filter.sort = QuerySort::Title;
  request.filter.descending = true;
  request.limit = 3;
  run();
  ASSERT_EQ(result.count, 3);
  EXPECT_EQ(result.matchedBooks, 7u);
  EXPECT_EQ(result.entries[0].book.key.ordinal, 39u);
  request.offset = 3;
  run();
  EXPECT_EQ(result.entries[0].book.key.ordinal, 30u);
}
TEST_F(QueryFixture, UnicodePredicateReportsUnsupported) {
  std::strcpy(request.filter.text.data(), "\xc3\xa9");
  run();
  EXPECT_EQ(result.availability, Availability::Unsupported);
}
TEST_F(QueryFixture, TenThousandBooksStayBoundedAndCancelPublishesNoPartialPage) {
  books = 10000;
  run();
  EXPECT_EQ(result.matchedBooks, 10000u);
  EXPECT_EQ(result.count, 12);
  ASSERT_TRUE(query.start(request));
  query.tick();
  query.cancel();
  query.copyResult(result);
  EXPECT_EQ(result.availability, Availability::Unavailable);
  EXPECT_EQ(result.count, 0);
}
TEST_F(QueryFixture, FacetOmitsItsOwnSelectionAndKeepsZeroCountChoices) {
  request.kind = QueryKind::Facets;
  request.field = QueryFacet::Author;
  request.filter.authors.count = 1;
  std::strcpy(request.filter.authors.values[0].data(), "Missing writer");
  run();
  ASSERT_EQ(result.availability, Availability::Ready);
  EXPECT_EQ(result.matchedBooks, 0u);
  ASSERT_EQ(result.count, 3);
  EXPECT_EQ(result.entries[0].books, 40u);
  EXPECT_STREQ(result.entries[2].label.data(), "Missing writer");
  EXPECT_EQ(result.entries[2].books, 0u);
  EXPECT_TRUE(result.entries[2].selected);
}
TEST_F(QueryFixture, FacetPagingUsesNonzeroThenAlphabeticOrder) {
  request.kind = QueryKind::Facets;
  request.field = QueryFacet::Author;
  request.filter.authors.count = 1;
  std::strcpy(request.filter.authors.values[0].data(), "AAA missing");
  request.offset = 1;
  request.limit = 1;
  run();
  ASSERT_EQ(result.count, 1);
  EXPECT_STREQ(result.entries[0].label.data(), "Writer B");
  EXPECT_TRUE(result.hasMore);
  EXPECT_EQ(result.optionTotal, 3u);
  request.offset = 2;
  run();
  EXPECT_STREQ(result.entries[0].label.data(), "AAA missing");
  EXPECT_FALSE(result.hasMore);
}
TEST_F(QueryFixture, ScalarFacetsMirrorAndroidBuckets) {
  request.kind = QueryKind::Facets;
  request.field = QueryFacet::Length;
  request.filter.pages = {300, 400, true, true};
  run();
  ASSERT_EQ(result.count, 4);
  EXPECT_EQ(result.matchedBooks, 0u);
  EXPECT_EQ(result.entries[0].books, 80u);
  EXPECT_EQ(result.entries[1].books, 80u);
  EXPECT_EQ(result.entries[2].books, 0u);
  request.field = QueryFacet::Where;
  request.filter.pages = {};
  run();
  ASSERT_EQ(result.count, 3);
  EXPECT_EQ(result.entries[1].books, 40u);
  request.field = QueryFacet::Show;
  request.filter.show = QueryShow::Finished;
  run();
  ASSERT_EQ(result.count, 5);
  EXPECT_EQ(result.entries[0].books, 80u);
  EXPECT_EQ(result.entries[2].books, 40u);
  request.field = QueryFacet::Rating;
  request.filter.show = QueryShow::All;
  run();
  ASSERT_EQ(result.count, 4);
  EXPECT_EQ(result.entries[2].books, 80u);
  request.field = QueryFacet::Added;
  request.utcDay = 20784;
  run();
  EXPECT_EQ(result.count, 4);
}
TEST_F(QueryFixture, SnapshotNeverPublishesPartialCounts) {
  ASSERT_TRUE(query.start(request));
  query.tick();
  query.copyResult(result);
  EXPECT_EQ(result.availability, Availability::NotSaved);
  EXPECT_EQ(result.matchedBooks, 0u);
  EXPECT_EQ(result.count, 0);
}
TEST(StoredQueries, CommittedCatalogAndDeviceSnapshotsAdvertiseOnlyProvenMetadata) {
  card = {};
  storage::ProjectionStore store(7, 1, {"fixture-server", "fixture-account"});
  CatalogRow row{};
  storage::IndexRecord index{};
  std::strcpy(row.alias.data(), "book-key");
  std::strcpy(row.title.data(), "Saved title");
  std::strcpy(row.authors[0].data(), "Writer");
  row.authorCount = 1;
  index.title = row.title;
  ASSERT_TRUE(store.begin());
  ASSERT_TRUE(store.append(row, index));
  ASSERT_TRUE(store.commit());
  StoredQuerySource source(store);
  Queries query(source.source());
  QueryRequest request{};
  QueryResult result{};
  request.filter.authors.count = 1;
  std::strcpy(request.filter.authors.values[0].data(), "writer");
  ASSERT_TRUE(query.start(request));
  while (query.busy()) query.tick();
  query.copyResult(result);
  EXPECT_EQ(result.availability, Availability::Ready);
  EXPECT_EQ(result.matchedBooks, 1u);
  request.filter.series.count = 1;
  std::strcpy(request.filter.series.values[0].data(), "Secondary");
  ASSERT_TRUE(query.start(request));
  query.copyResult(result);
  EXPECT_EQ(result.availability, Availability::Unsupported);
  EXPECT_EQ(card.handles, 0);
}

TEST_F(QueryFixture, SecondarySeriesSortUsesContextualDecimalPositions) {
  std::strcpy(request.seriesKey.data(), "series-secondary");
  request.filter.sort = QuerySort::Series;
  request.limit = 3;
  run();
  ASSERT_EQ(result.availability, Availability::Ready);
  EXPECT_EQ(result.entries[0].book.key.ordinal, 79u);
  EXPECT_EQ(result.entries[1].book.key.ordinal, 78u);
}
TEST_F(QueryFixture, GroupPagesCountWholeGroups) {
  request.kind = QueryKind::Groups;
  request.filter.group = QueryGroup::Author;
  request.limit = 1;
  request.offset = 1;
  run();
  ASSERT_EQ(result.count, 1);
  EXPECT_STREQ(result.entries[0].label.data(), "Writer B");
  EXPECT_EQ(result.entries[0].books, 40u);
}
TEST_F(QueryFixture, CountsIncludeStatusesAndUnknownDeviceIsSeparate) {
  request.kind = QueryKind::Counts;
  run();
  EXPECT_EQ(result.matchedBooks, 80u);
  EXPECT_EQ(result.count, 0);
  EXPECT_TRUE(result.statusCountsKnown);
  EXPECT_EQ(result.statuses[unsigned(QueryShow::Rereading)], 40u);
  EXPECT_TRUE(result.deviceCountsKnown);
  EXPECT_EQ(result.deviceCounts[unsigned(QueryTruth::Yes)], 40u);
}

TEST_F(QueryFixture, EveryPageBoundaryHasCorrectCountAndMoreFlag) {
  for (unsigned limit : {1u, 5u, 12u, 24u})
    for (unsigned offset : {0u, 1u, 5u, 23u, 24u, 25u, 79u, 80u, 81u, 10000u}) {
      request.offset = offset;
      request.limit = limit;
      run();
      const auto expected = offset >= books ? 0u : std::min(limit, books - offset);
      ASSERT_EQ(result.availability, Availability::Ready);
      ASSERT_EQ(result.count, expected);
      EXPECT_EQ(result.hasMore, offset + expected < books);
      if (expected) EXPECT_EQ(result.entries[0].book.key.ordinal, offset);
    }
}
TEST_F(QueryFixture, PublishedResultRetiresOnCaptureOrAccountChange) {
  run();
  ASSERT_EQ(result.availability, Availability::Ready);
  ++generation;
  query.copyResult(result);
  EXPECT_EQ(result.availability, Availability::Unavailable);
  run();
  query.invalidate();
  query.copyResult(result);
  EXPECT_EQ(result.count, 0);
}
TEST_F(QueryFixture, ScalarFacetSelectionsUseExactAndroidRanges) {
  request.kind = QueryKind::Facets;
  request.field = QueryFacet::Rating;
  request.filter.rating = {4, 0, true, false};
  run();
  ASSERT_EQ(result.count, 4);
  EXPECT_TRUE(result.entries[2].selected);
  EXPECT_FALSE(result.entries[1].selected);
  request.field = QueryFacet::Added;
  request.filter.rating = {};
  request.utcDay = 20784;
  std::strcpy(request.filter.added.min.data(), "2026-11-20");
  run();
  ASSERT_EQ(result.count, 4);  // Nov 27, 2026 is epoch day 20784; week starts Nov 21.
  EXPECT_FALSE(result.entries[1].selected);
}

TEST_F(QueryFixture, UnknownDeviceIsNeverServerOnlyAndFacetDuplicatesCountOnce) {
  auto original = source();
  original.read = [](void* p, BookKey key, QueryRecord& row) {
    if (!static_cast<QueryFixture*>(p)->source().read(p, key, row)) return false;
    row.onDevice = QueryTruth::Unknown;
    row.tags[1] = row.tags[0];
    row.tagCount = 2;
    return true;
  };
  Queries worker(original);
  request.filter.where = QueryWhere::ServerOnly;
  ASSERT_TRUE(worker.start(request));
  while (worker.busy()) worker.tick();
  worker.copyResult(result);
  EXPECT_EQ(result.availability, Availability::Ready);
  EXPECT_EQ(result.matchedBooks, 0u);
  request.filter.where = QueryWhere::Anywhere;
  request.kind = QueryKind::Facets;
  request.field = QueryFacet::Tags;
  ASSERT_TRUE(worker.start(request));
  while (worker.busy()) worker.tick();
  worker.copyResult(result);
  ASSERT_EQ(result.count, 1);
  EXPECT_EQ(result.entries[0].books, 80u);
}
TEST_F(QueryFixture, UnusedSortAndGroupingDoNotMakeCountsUnsupported) {
  fields = 0;
  request.kind = QueryKind::Counts;
  request.filter.group = QueryGroup::Author;
  request.filter.sort = QuerySort::Match;
  run();
  EXPECT_EQ(result.availability, Availability::Ready);
  EXPECT_EQ(result.matchedBooks, 80u);
  EXPECT_FALSE(result.statusCountsKnown);
  EXPECT_FALSE(result.deviceCountsKnown);
}

TEST_F(QueryFixture, SupersededRequestsCannotPublishTheOldSelection) {
  request.requestId = 11;
  ASSERT_TRUE(query.start(request));
  query.tick();
  request.requestId = 12;
  request.filter.where = QueryWhere::OnDevice;
  run();
  EXPECT_EQ(result.requestId, 12u);
  EXPECT_EQ(result.matchedBooks, 40u);
}

TEST(StoredQueries, NativeReadingPagesDoNotClaimBibliographicLength) {
  card = {};
  storage::Index index({"/length-index", "/length-index.tmp", "/length-index.bak"}, "/length-a", "/length-b");
  storage::IndexRecord row{};
  std::strcpy(row.path.data(), "/fixture.epub");
  std::strcpy(row.title.data(), "Saved title");
  row.readingKnown = true;
  row.page = 42;
  row.pages = 900;
  ASSERT_TRUE(index.begin({1, 1, 0, 0}));
  ASSERT_TRUE(index.append(row));
  ASSERT_TRUE(index.commit());
  StoredQuerySource source(index);
  Queries query(source.source());
  QueryRequest request{};
  QueryResult result{};
  request.filter.pages = {250, 450, true, true};
  ASSERT_TRUE(query.start(request));
  query.copyResult(result);
  EXPECT_EQ(result.availability, Availability::Unsupported);
  EXPECT_EQ(result.count, 0);
}

}  // namespace
