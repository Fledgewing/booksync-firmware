// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/Queries.h"
namespace booksync {
using QueryFields = uint32_t;
enum class QueryField : uint8_t {
  Title,
  Author,
  Series,
  Tags,
  Status,
  WantToRead,
  Device,
  Pages,
  Rating,
  Added,
  Last,
  SeriesPosition,
  Match,
  Scope,
  SeriesIdentity
};
constexpr QueryFields queryField(QueryField field) { return 1u << unsigned(field); }
constexpr QueryFields QueryAllFields = (1u << 15) - 1;
struct QueryRecord {
  BookRow book{};
  uint32_t lengthPages = 0;  // Bibliographic length, independent of native rendered pages.
  std::array<std::array<char, 96>, 8> authors{}, series{}, tags{};
  std::array<std::array<char, 64>, 8> scopes{}, seriesKeys{};
  std::array<double, 8> seriesPositions{};
  std::array<uint32_t, 8> seriesRanks{};
  std::array<bool, 8> seriesPositionsKnown{};
  std::array<char, 40> added{}, last{};
  uint8_t authorCount = 0, seriesCount = 0, tagCount = 0, scopeCount = 0;
  QueryShow status = QueryShow::Unknown;
  QueryTruth onDevice = QueryTruth::Unknown;
  double rating = 0, seriesPosition = 0, match = 0;
  bool wantToRead = false, pagesKnown = false, ratingKnown = false, seriesPositionKnown = false, matchKnown = false;
  void clear() {
    book = {};
    lengthPages = 0;
    authors.fill({});
    series.fill({});
    tags.fill({});
    scopes.fill({});
    seriesKeys.fill({});
    seriesPositions.fill(0);
    seriesRanks.fill(0);
    seriesPositionsKnown.fill(false);
    added.fill(0);
    last.fill(0);
    authorCount = seriesCount = tagCount = scopeCount = 0;
    status = QueryShow::Unknown;
    onDevice = QueryTruth::Unknown;
    rating = seriesPosition = match = 0;
    wantToRead = pagesKnown = ratingKnown = seriesPositionKnown = matchKnown = false;
  }
};
struct QueryState {
  Provenance provenance{};
  uint32_t count = 0;
  QueryFields fields = 0;  // A set bit promises COMPLETE metadata, including secondary memberships.
  bool ready = false;
};
// state() is a cheap in-memory snapshot; read() is worker-only I/O.
struct QuerySource {
  void* context = nullptr;
  QueryState (*state)(void*) = nullptr;
  bool (*read)(void*, BookKey, QueryRecord&) = nullptr;
};
// Worker-only, externally serialized with generation/capture writers. No I/O in
// copyResult. Fixed query workspace; source/HAL handle lifetimes remain external.
// Sorting uses bounded
// repeated passes; each tick reads at most Batch records, including deep paging.
class Queries {
 public:
  static constexpr uint16_t Batch = 16;
  explicit Queries(QuerySource source) : source_(source) {}
  bool start(const QueryRequest& request);
  // Reuse the existing worker request storage, avoiding a multi-KiB stack copy.
  bool startSavedScope(const QueryRequest& request, const std::array<char, 64>& scope) {
    request_ = request;
    request_.scope = scope;
    return start(request_);
  }
  bool tick(uint16_t allowance = Batch);
  void cancel();
  void invalidate() { finish(Availability::Unavailable); }
  bool busy() const { return busy_; }
  uint16_t lastWork() const { return lastWork_; }
  void copyResult(QueryResult& result) const {
    if (busy_) {
      result.clear();
      result.provenance = state_.provenance;
      result.requestId = request_.requestId;
      result.pending = true;
    } else {
      result = result_;
      if (result.availability == Availability::Ready) {
        const auto now = source_.state(source_.context);
        if (!now.ready || now.provenance != result.provenance || now.fields != state_.fields ||
            now.count != state_.count) {
          result.clear();
          result.requestId = request_.requestId;
          result.availability = Availability::Unavailable;
        }
      }
    }
  }

 private:
  struct Ranked {
    QueryEntry value{};
    std::array<char, 96> order{};
    double number = 0;
    uint32_t sourceRank = 0;
    bool known = true;
  };
  QuerySource source_{};
  QueryState state_{};
  QueryRequest request_{};
  QueryResult result_{};
  QueryRecord row_{};
  std::array<Ranked, LibraryRows> selected_{};
  Ranked candidate_{}, cursor_{};
  std::array<char, 96> labelCursor_{}, nextLabel_{};
  uint32_t labelBooks_ = 0;
  uint8_t scalarOption_ = 0;
  bool labelFound_ = false, discovering_ = true;
  uint32_t ordinal_ = 0, skipped_ = 0;
  uint16_t selectedCount_ = 0, lastWork_ = 0;
  bool busy_ = false, cursorSet_ = false, countingFacets_ = false, firstPass_ = true, extra_ = false;
  inline static uint64_t publication_ = 0;
  QueryFields required() const;
  bool matches(bool omitFacet = false) const;
  bool inScope() const;
  bool facetOptionMatches(uint8_t) const;
  bool facetOptionSelected(uint8_t) const;
  void seedLabel();
  void advanceLabel();
  bool validRow() const;
  void clearCandidate();
  void rank();
  int compare(const Ranked&, const Ranked&) const;
  void insert();
  void projectLabels();
  void finishPass();
  void finish(Availability);
};
static_assert(sizeof(Queries) <= 32 * 1024);
}  // namespace booksync

#include "booksync/storage/ProjectionStore.h"
namespace booksync {
// The persisted source cannot prove complete secondary-series/tag/status
// metadata. It advertises only fields actually present; Queries rejects other
// predicates before scanning, including search which needs complete series.
class StoredQuerySource {
 public:
  explicit StoredQuerySource(storage::Index& index) : index_(index) {}
  explicit StoredQuerySource(storage::ProjectionStore& store) : index_(store.index()), catalog_(&store) {}
  QuerySource source() { return {this, state, read}; }

 private:
  storage::Index& index_;
  storage::ProjectionStore* catalog_ = nullptr;
  CatalogRow catalogRow_{};
  static QueryState state(void*);
  static bool read(void*, BookKey, QueryRecord&);
};
static_assert(sizeof(StoredQuerySource) <= 16 * 1024);
}  // namespace booksync
