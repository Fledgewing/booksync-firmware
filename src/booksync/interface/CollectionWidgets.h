// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>

#include "booksync/interface/Identity.h"
namespace booksync {
enum class CollectionWidgetState : uint8_t { NotSaved, Ready, Empty, Off, Unsupported, Error, Oversize };
struct LibraryWidgetValues {
  Provenance provenance{};
  uint64_t freeBytes = 0, totalBytes = 0;
  uint32_t books = 0, onCard = 0, authors = 0, series = 0, collections = 0, ageDays = 0, bytes = 0;
  CollectionWidgetState state = CollectionWidgetState::NotSaved;
  uint8_t rows = 0, storageUsedPercent = 0;
  bool booksKnown = false, onCardKnown = false, authorsKnown = false, seriesKnown = false, collectionsKnown = false,
       storageKnown = false, stale = false, ageKnown = false;
};
static_assert(sizeof(LibraryWidgetValues) <= 96);
// Process-local saved-list identity. The worker alone resolves it to a source
// membership; no server identifier, rule, path or mutable handle reaches UI.
struct ListKey {
  Provenance provenance{};
  uint32_t ordinal = 0;
  bool operator==(ListKey other) const { return provenance == other.provenance ? ordinal == other.ordinal : false; }
};
struct ListWidgetEntry {
  ListKey key{};
  std::array<char, 64> name{};
  uint32_t books = 0;
  bool booksKnown = false;
};
static_assert(sizeof(ListWidgetEntry) <= 112);
struct ListsWidgetValues {
  static constexpr uint8_t MaxRows = 3;
  std::array<ListWidgetEntry, MaxRows> entries{};
  Provenance provenance{};
  uint32_t total = 0, ageDays = 0, bytes = 0;
  CollectionWidgetState state = CollectionWidgetState::NotSaved;
  uint8_t count = 0;
  bool countKnown = false, complete = false, stale = false, ageKnown = false;
  void clear() {
    for (uint8_t i = 0; i < MaxRows; ++i) entries[i] = {};  // One <=112-byte temporary, never a whole snapshot.
    provenance = {};
    total = ageDays = bytes = count = 0;
    state = CollectionWidgetState::NotSaved;
    countKnown = complete = stale = ageKnown = false;
  }
};
static_assert(sizeof(ListsWidgetValues) <= 448);
}  // namespace booksync
