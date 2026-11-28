// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Models.h"
namespace booksync {
// Neutral Android filter-sheet rows: show, where, author, series, tags,
// length, rating, added, grouping. Unsupported metadata stays explicit.
enum class QueryShow : uint8_t { All, WantToRead, Unread, Reading, Finished, Rereading, OnHold, Abandoned, Unknown };
enum class QueryTruth : uint8_t { Unknown, No, Yes };
enum class QueryWhere : uint8_t { Anywhere, OnDevice, ServerOnly };
enum class QueryGroup : uint8_t { None, Author, Series };
enum class QuerySort : uint8_t { Order, Match, Title, Author, Last, Added, Rating, Series };
enum class QueryKind : uint8_t { Books, Counts, Facets, Groups };
enum class QueryFacet : uint8_t { Author, Series, Tags, Show, Where, Length, Rating, Added };
struct QuerySelection {
  std::array<std::array<char, 96>, 8> values{};
  uint8_t count = 0;
};
struct QueryRange {
  double min = 0, max = 0;
  bool hasMin = false, hasMax = false;
};
struct QueryDateRange {
  std::array<char, 40> min{}, max{};
};
struct QueryFilter {
  QuerySelection authors{}, series{}, tags{};
  std::array<char, 513> text{};
  QueryRange pages{}, rating{};
  QueryDateRange added{};
  QueryShow show = QueryShow::All;
  QueryWhere where = QueryWhere::Anywhere;
  QueryGroup group = QueryGroup::None;
  QuerySort sort = QuerySort::Order;
  bool wantToRead = false, withoutSeries = false, descending = false;
};
struct QueryRequest {
  uint64_t requestId = 0;  // Caller correlation; never a durable job or server identity.
  QueryFilter filter{};
  std::array<char, 64> seriesKey{};  // Opaque contextual membership, including secondary series.
  std::array<char, 64> scope{};      // Opaque saved scope membership, never evaluated rules.
  uint32_t offset = 0, bytes = LibraryBytes;
  int32_t utcDay = 0;  // UTC days since Unix epoch; required for relative added facets.
  uint16_t limit = 12;
  QueryKind kind = QueryKind::Books;
  QueryFacet field = QueryFacet::Author;
};
struct QueryEntry {
  BookRow book{};
  std::array<char, 96> label{};
  std::array<char, 64> groupKey{};
  uint32_t books = 0;
  uint8_t option = 0;  // Scalar facet option index; labels are translated by UI.
  double seriesPosition = 0;
  QueryShow status = QueryShow::Unknown;
  QueryTruth onDevice = QueryTruth::Unknown;
  double rating = 0;
  bool ratingKnown = false, wantToRead = false;
  bool seriesPositionKnown = false;
  bool selected = false;
  bool missing = false;  // UI translates the Unknown author / No series labels.
  void clear() {
    // The only temporary is one small BookRow, never a whole result page.
    static_assert(sizeof(BookRow) <= 256);
    book = {};
    label.fill(0);
    groupKey.fill(0);
    books = 0;
    option = 0;
    seriesPosition = rating = 0;
    status = QueryShow::Unknown;
    onDevice = QueryTruth::Unknown;
    ratingKnown = wantToRead = seriesPositionKnown = selected = missing = false;
  }
};
struct QueryResult {
  std::array<QueryEntry, LibraryRows> entries{};
  Provenance provenance{};
  uint64_t publication = 0, requestId = 0;
  uint32_t matchedBooks = 0, optionTotal = 0, bytes = 0;
  uint16_t count = 0;
  Availability availability = Availability::NotSaved;
  std::array<uint32_t, 9> statuses{};
  std::array<uint32_t, 3> deviceCounts{};
  bool statusCountsKnown = false, deviceCountsKnown = false;
  bool hasMore = false;
  void clear() {
    for (uint16_t i = 0; i < LibraryRows; ++i) entries[i].clear();
    provenance = {};
    publication = requestId = 0;
    matchedBooks = optionTotal = bytes = 0;
    count = 0;
    availability = Availability::NotSaved;
    statuses.fill(0);
    deviceCounts.fill(0);
    statusCountsKnown = deviceCountsKnown = hasMore = false;
  }
};
static_assert(sizeof(QueryResult) <= LibraryBytes);
}  // namespace booksync
