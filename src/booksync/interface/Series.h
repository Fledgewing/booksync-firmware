// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/Queries.h"
namespace booksync {
constexpr uint8_t SeriesNextMax = 10;
struct SeriesRequest {
  std::array<char, 64> key{};
  BookKey current{}, start{};
  uint64_t requestId = 0;
  uint8_t count = 3;
  bool currentKnown = false, startKnown = false;
};
struct SeriesMember {
  BookRow book{};
  std::array<char, 96> author{};
  double position = 0;
  uint32_t rank = 0;
  QueryShow status = QueryShow::Unknown;
  QueryTruth onDevice = QueryTruth::Unknown;
  bool positionKnown = false;
  void clear() {
    book = {};
    author.fill(0);
    position = 0;
    rank = 0;
    status = QueryShow::Unknown;
    onDevice = QueryTruth::Unknown;
    positionKnown = false;
  }
};
// Published immutable values only. The worker owns selection; UI never scans a
// catalog or infers eligibility from a partial membership window.
struct SeriesPlanView {
  std::array<SeriesMember, SeriesNextMax> next{};
  SeriesMember earlier{};
  Provenance provenance{};
  std::array<char, 96> title{}, author{};
  uint64_t requestId = 0, token = 0;
  uint32_t total = 0, read = 0, reading = 0;
  uint8_t count = 0, requested = 3;
  Availability availability = Availability::NotSaved;
  bool pending = false, earlierKnown = false, membershipIncomplete = false;
  bool deviceKnown = false, remainingSaved = false;
  void clear() {
    for (uint8_t i = 0; i < SeriesNextMax; ++i) next[i].clear();
    earlier.clear();
    provenance = {};
    title.fill(0);
    author.fill(0);
    requestId = token = 0;
    total = read = reading = 0;
    count = 0;
    requested = 3;
    availability = Availability::NotSaved;
    pending = earlierKnown = membershipIncomplete = deviceKnown = remainingSaved = false;
  }
};
static_assert(sizeof(SeriesPlanView) <= LibraryBytes);
}  // namespace booksync
