// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdio>
#include <cstring>

#include "booksync/services/Queries.h"
namespace booksync::test {
// Synthetic complete metadata layered over a real committed F21 generation.
// Production F21 advertises its missing metadata as unsupported; this fixture
// tests the neutral engine contract without pretending the cache is richer.
struct OfflineQuerySource {
  storage::ProjectionStore& store;
  explicit OfflineQuerySource(storage::ProjectionStore& store) : store(store) {}
  QuerySource source() {
    return {this,
            [](void* p) {
              auto& self = *static_cast<OfflineQuerySource*>(p);
              return QueryState{self.store.index().provenance(), self.store.index().count(), QueryAllFields,
                                self.store.index().ready()};
            },
            [](void* p, BookKey key, QueryRecord& row) {
              auto& self = *static_cast<OfflineQuerySource*>(p);
              row = {};
              if (!self.store.index().summary(key, row.book)) return false;
              std::strcpy(row.authors[0].data(), key.ordinal % 2 ? "Writer B" : "Writer A");
              row.authorCount = 1;
              std::strcpy(row.series[0].data(), "Primary");
              std::strcpy(row.series[1].data(), "Secondary");
              row.seriesCount = 2;
              std::strcpy(row.seriesKeys[0].data(), key.ordinal % 2 ? "primary-b" : "primary-a");
              std::strcpy(row.seriesKeys[1].data(), "secondary");
              row.seriesPositionsKnown[1] = true;
              row.seriesPositions[1] = double(80 - key.ordinal) / 2;
              row.seriesRanks[1] = key.ordinal;
              std::strcpy(row.tags[0].data(), "Saved");
              row.tagCount = 1;
              std::strcpy(row.scopes[0].data(), key.ordinal % 2 ? "scope-b" : "scope-a");
              row.scopeCount = 1;
              row.status = key.ordinal % 2 ? QueryShow::Rereading : QueryShow::Unread;
              row.onDevice = key.ordinal % 2 ? QueryTruth::No : QueryTruth::Yes;
              row.wantToRead = key.ordinal % 3 == 0;
              row.pagesKnown = row.ratingKnown = true;
              row.book.pages = 900;
              row.lengthPages = 100 + key.ordinal;
              row.rating = 4;
              std::strcpy(row.added.data(), "2026-11-01");
              return true;
            }};
  }
};
}  // namespace booksync::test
