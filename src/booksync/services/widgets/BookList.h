// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/services/PersonalReading.h"
namespace booksync {
// A frozen, already-indexed selection. The provider owns filtering, membership,
// ordering and authoritative counts. bytes covers this bounded decoded preview,
// never the size of the entire catalog. Native owner zero is account independent.
struct BookListMeta {
  Provenance provenance{};
  uint32_t total = 0, saved = 0, bytes = 0, ageDays = 0;
  uint16_t year = 0;
  PersonalState state = PersonalState::NotSaved;
  bool countKnown = false, complete = false, stale = false, ageKnown = false;
  bool operator==(const BookListMeta&) const = default;
};
struct BookListSource {
  void* context = nullptr;
  BookListMeta (*peek)(void*, WindowSource) = nullptr;
  bool (*read)(void*, WindowSource, uint32_t rank, BookRow&) = nullptr;
  FileAvailability (*file)(void*, WindowSource, uint32_t rank) = nullptr;
};
struct BookListProjection {
  static constexpr uint16_t MaxRows = 4;
  std::array<BookRow, MaxRows> books{};
  std::array<FileAvailability, MaxRows> files{};
  Provenance provenance{};
  WindowRequest request{};
  uint32_t total = 0, bytes = 0, ageDays = 0;
  uint16_t count = 0, year = 0;
  PersonalState state = PersonalState::NotSaved;
  bool stale = false, complete = false, ageKnown = false;
  void clear();
};
static_assert(sizeof(BookListProjection) <= 1024);
// Caller owns reusable output; only a maximum of four indexed records is read.
// Also reading and On device use three rows; the seven cover presets use four.
// No UI or transport is activated by this worker-only projection.
bool projectBookList(BookListSource, WindowRequest, ReadingScope current, uint16_t currentYear,
                     BookListProjection& out);
}  // namespace booksync
