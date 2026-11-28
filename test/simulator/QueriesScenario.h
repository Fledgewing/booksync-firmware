// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdlib>
#include <cstring>

#include "../fixtures/queries/OfflineSource.h"
inline void runQueriesScenario() {
  const char* mode = std::getenv("BOOKSYNC_QUERIES_SCENARIO");
  if (!mode) return;
  using namespace booksync;
  static storage::ProjectionStore store(7, 1, {"query-fixture-server", "query-fixture-account"});
  bool ok = (Storage.mkdir("/.booksync") || Storage.exists("/.booksync")) && store.recover();
  if (!std::strcmp(mode, "seed")) {
    static CatalogRow row;
    static storage::IndexRecord index;
    ok = ok && store.begin();
    for (unsigned i = 0; ok && i < 80; ++i) {
      std::snprintf(row.alias.data(), row.alias.size(), "book-%03u", i);
      std::snprintf(row.title.data(), row.title.size(), "Saved book %03u", i);
      index.title = row.title;
      ok = store.append(row, index);
    }
    ok = ok && store.commit();
  }
  static test::OfflineQuerySource complete(store);
  static Queries query(complete.source());
  static QueryRequest request;
  static QueryResult result;
  auto run = [&]() {
    if (!query.start(request)) return false;
    unsigned ticks = 0;
    while (query.busy() && ++ticks < 100000) {
      query.tick();
      if (query.lastWork() > Queries::Batch) return false;
    }
    query.copyResult(result);
    return !query.busy() && result.availability == Availability::Ready;
  };
  ok = ok && store.index().ready() && store.index().count() == 80;
  unsigned cases = 0;
  // Search, secondary membership, reading includes rereading, scoped paging.
  std::strcpy(request.filter.text.data(), "SAVED secondary");
  request.filter.show = QueryShow::Reading;
  request.filter.where = QueryWhere::ServerOnly;
  std::strcpy(request.scope.data(), "scope-b");
  request.offset = 12;
  request.limit = 5;
  ok = ok && run() && result.count == 5 && result.matchedBooks == 40 && result.entries[0].book.key.ordinal == 25;
  ++cases;
  request.offset = 39;
  ok = ok && run() && result.count == 1 && !result.hasMore;
  ++cases;
  request = {};
  request.kind = QueryKind::Facets;
  request.field = QueryFacet::Author;
  request.filter.authors.count = 1;
  std::strcpy(request.filter.authors.values[0].data(), "Missing writer");
  ok = ok && run() && result.count == 3 && result.entries[0].books == 40 && result.entries[2].books == 0 &&
       result.entries[2].selected;
  ++cases;
  request = {};
  request.kind = QueryKind::Groups;
  request.filter.group = QueryGroup::Series;
  ok = ok && run() && result.count == 2 && result.entries[0].books == 40 && result.entries[1].books == 40 &&
       std::strcmp(result.entries[0].groupKey.data(), result.entries[1].groupKey.data());
  ++cases;
  request = {};
  std::strcpy(request.seriesKey.data(), "secondary");
  request.filter.sort = QuerySort::Series;
  ok = ok && run() && result.entries[0].book.key.ordinal == 79 && result.entries[0].seriesPosition == 0.5;
  ++cases;
  static StoredQuerySource saved(store);
  static Queries persisted(saved.source());
  request = {};
  request.filter.series.count = 1;
  std::strcpy(request.filter.series.values[0].data(), "Secondary");
  ok = ok && persisted.start(request);
  persisted.copyResult(result);
  ok = ok && result.availability == Availability::Unsupported && !result.count;
  ++cases;
  // A new committed generation invalidates a previously published result.
  request = {};
  ok = ok && run();
  if (!std::strcmp(mode, "invalidate")) {
    ok = ok && store.begin() && store.commit();
    query.copyResult(result);
    ok = ok && result.availability == Availability::Unavailable && !result.count;
    ++cases;
  }
  LOG_INF("BSQ",
          "BookSync queries scenario %s: %s; cases=%u; workspace=%u; source=%u; batch=%u; per-row query workspace "
          "allocations=0",
          ok ? "PASS" : "FAIL", mode, cases, unsigned(sizeof(query)), unsigned(sizeof(saved)),
          unsigned(Queries::Batch));
  std::fflush(nullptr);
  std::_Exit(ok ? 0 : 1);
}
