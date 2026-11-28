// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Models.h"
namespace booksync {
enum class PersonalWidget : uint8_t { Highlight, YearAgo, Recommended, Discover };
enum class PersonalCardState : uint8_t { NotSaved, Ready, Empty, Unsupported, Unavailable, Off, Oversize };
enum class PersonalOrigin : uint8_t { Unknown, ThisReader, Account };
struct PersonalStatus {
  uint64_t owner = 0;
  uint32_t accountEpoch = 0, bytes = 0, total = 0, ageDays = 0;
  PersonalCardState state = PersonalCardState::NotSaved;
  PersonalOrigin origin = PersonalOrigin::Unknown;
  bool stale = false;
};
struct HighlightWidget {
  PersonalStatus status{};
  BookRow book{};
  std::array<char, 901> passage{};
  std::array<char, 161> source{};
  bool canOpen = false;
  void clear() {
    status = {};
    book = {};
    passage.fill(0);
    source.fill(0);
    canOpen = false;
  }
};
struct YearAgoWidget {
  PersonalStatus status{};
  BookRow book{};
  uint64_t seconds = 0;
  uint32_t days = 0, stop = 0, pages = 0;
  int16_t year = 0;
  uint8_t month = 0, day = 0;
  bool secondsKnown = false, daysKnown = false, stopKnown = false;
  void clear() {
    status = {};
    book = {};
    seconds = 0;
    days = stop = pages = 0;
    year = 0;
    month = day = 0;
    secondsKnown = daysKnown = stopKnown = false;
  }
};
struct RecommendedWidget {
  PersonalStatus status{};
  BookRow book{};
  std::array<char, 161> source{};
};
struct DiscoverWidget {
  PersonalStatus status{};
  std::array<BookRow, 4> books{};
  uint8_t count = 0;
};
struct PersonalWidgetBatch {
  uint64_t publication = 0;
  int64_t localDay = 0;
  HighlightWidget highlight{};
  YearAgoWidget yearAgo{};
  RecommendedWidget recommended{};
  DiscoverWidget discover{};
  uint8_t requested = 0;
  void clear() {
    publication = 0;
    localDay = 0;
    highlight.clear();
    yearAgo.clear();
    recommended.status = {};
    recommended.book = {};
    recommended.source.fill(0);
    discover.status = {};
    discover.books.fill({});
    discover.count = 0;
    requested = 0;
  }
};
constexpr uint32_t PersonalBatchMetadata = sizeof(PersonalWidgetBatch) - sizeof(HighlightWidget) -
                                           sizeof(YearAgoWidget) - sizeof(RecommendedWidget) - sizeof(DiscoverWidget);
constexpr uint8_t personalWidgetBit(PersonalWidget widget) {
  return static_cast<unsigned>(widget) < 4 ? 1u << static_cast<unsigned>(widget) : 0;
}
constexpr uint16_t personalWidgetRows(uint8_t mask) {
  return (((mask / 1) % 2) ? 1 : 0) + (((mask / 2) % 2) ? 1 : 0) + (((mask / 4) % 2) ? 1 : 0) +
         (((mask / 8) % 2) ? 4 : 0);
}
constexpr uint32_t personalWidgetBytes(uint8_t mask) {
  return !mask
             ? 0
             : PersonalBatchMetadata + (((mask / 1) % 2) ? sizeof(HighlightWidget) : 0) +
                   (((mask / 2) % 2) ? sizeof(YearAgoWidget) : 0) + (((mask / 4) % 2) ? sizeof(RecommendedWidget) : 0) +
                   (((mask / 8) % 2) ? sizeof(DiscoverWidget) : 0);
}
static_assert(sizeof(PersonalWidgetBatch) <= 4096);

enum class PersonalWidgetState : uint8_t { NotSaved, Ready, Empty, Off, Unsupported, Error, Oversize };
struct PersonalWidgetValues {
  static constexpr uint8_t MaxBooks = 4;
  std::array<BookRow, MaxBooks> books{};
  uint32_t total = 0, bytes = 0, ageDays = 0;
  PersonalWidgetState state = PersonalWidgetState::NotSaved;
  uint8_t count = 0, rows = 0;
  bool stale = false, ageKnown = false;
  void clear() {
    for (uint8_t i = 0; i < MaxBooks; ++i) books[i] = {};
    total = bytes = ageDays = count = rows = 0;
    state = PersonalWidgetState::NotSaved;
    stale = ageKnown = false;
  }
};
static_assert(sizeof(PersonalWidgetValues) <= 1024);
}  // namespace booksync
