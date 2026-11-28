// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/CollectionWidgets.h"
#include "booksync/services/PersonalReading.h"
namespace booksync {
struct SavedListsRequest {
  uint32_t offset = 0, bytes = 16384;
  uint8_t limit = 12;
};
struct SavedListsPage {
  static constexpr uint8_t MaxRows = 12;
  std::array<ListWidgetEntry, MaxRows> entries{};
  Provenance provenance{};
  uint64_t publication = 0;
  uint32_t total = 0, saved = 0, offset = 0, bytes = 0;
  CollectionWidgetState state = CollectionWidgetState::NotSaved;
  uint8_t count = 0;
  bool countKnown = false, hasMore = false, complete = false;
  void clear() {
    for (uint8_t i = 0; i < MaxRows; ++i) entries[i] = {};
    provenance = {};
    publication = 0;
    total = saved = offset = bytes = count = 0;
    state = CollectionWidgetState::NotSaved;
    countKnown = hasMore = complete = false;
  }
};
static_assert(sizeof(SavedListsPage) <= 1536);

struct CollectionAllowance {
  uint32_t bytes = LibraryBytes;
  uint16_t rows = 3;
};
// Already-resolved, immutable count and storage rows. Known zero remains a
// count; the native on-card count never substitutes for a missing library total.
struct LibraryWidgetInput {
  Provenance provenance{};
  ReadingValue books{}, onCard{}, authors{}, series{}, collections{};
  uint64_t freeBytes = 0, totalBytes = 0;
  uint32_t bytes = 0, ageDays = 0;
  PersonalState state = PersonalState::NotSaved;
  bool storageKnown = false, stale = false, ageKnown = false;
};
bool projectLibraryWidget(const LibraryWidgetInput&, ReadingScope current, CollectionAllowance,
                          LibraryWidgetValues& out);
struct ListsWidgetMeta {
  Provenance provenance{};
  uint32_t total = 0, saved = 0, bytes = 0, ageDays = 0;
  PersonalState state = PersonalState::NotSaved;
  bool countKnown = false, complete = false, stale = false, ageKnown = false;
  bool operator==(const ListsWidgetMeta&) const = default;
};
struct ListsWidgetSource {
  void* context = nullptr;
  ListsWidgetMeta (*peek)(void*) = nullptr;
  bool (*read)(void*, uint32_t rank, ListWidgetEntry&) = nullptr;
  bool (*membership)(void*, ListKey, std::array<char, 64>& scope) = nullptr;
};
// Main asks for one saved list; the approved optional size asks for three.
// Indexed records and metadata share one frozen snapshot, rechecked before publish.
bool projectListsWidget(ListsWidgetSource, ReadingScope current, CollectionAllowance, ListsWidgetValues& out);
bool projectSavedListsPage(ListsWidgetSource, ReadingScope current, SavedListsRequest, SavedListsPage& out);
}  // namespace booksync
