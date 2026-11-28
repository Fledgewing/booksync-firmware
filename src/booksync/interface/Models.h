#pragma once

#include <array>
#include <cstdint>

#include "booksync/interface/ContinueValues.h"

namespace booksync {
// Presentation values, never wire IDs, credentials or mutable store handles.
// Identity bytes come from F08 evidence; unknown values remain explicit.
enum class Availability : uint8_t { NotSaved, Ready, Unsupported, Unavailable };
struct Snapshot {
  Availability catalog = Availability::NotSaved;
  uint32_t generation = 0;
  uint32_t accountEpoch = 0;
  bool canOpenLastBook = false;
  bool upgradeNoticePending = false;
  bool upgradeRecoveryRequired = false;
};
}  // namespace booksync

namespace booksync {
constexpr uint16_t LibraryRows = 24;
constexpr uint32_t LibraryBytes = 16384;
struct Provenance {
  uint32_t generation = 0, accountEpoch = 1;
  uint64_t owner = 0, capture = 0;
  bool operator==(Provenance other) const {
    if (generation != other.generation) return false;
    if (accountEpoch != other.accountEpoch) return false;
    if (owner != other.owner) return false;
    return capture == other.capture;
  }
};
struct BookKey {
  Provenance provenance{};
  uint32_t ordinal = 0;
  bool operator==(BookKey other) const { return provenance == other.provenance ? ordinal == other.ordinal : false; }
};
enum class WindowSource : uint8_t {
  OnDevice,
  Continue,
  Recent,
  UpNext,
  WantToRead,
  Unopened,
  Random,
  FinishedYear,
  PickBackUp,
  Unknown
};
struct WindowRequest {
  uint32_t offset = 0;
  uint16_t limit = 12;
  uint32_t bytes = LibraryBytes;
  WindowSource source = WindowSource::OnDevice;
  bool operator==(WindowRequest other) const {
    if (source != other.source || offset != other.offset) return false;
    return limit == other.limit ? bytes == other.bytes : false;
  }
};
constexpr uint8_t LibraryBatchRequests = 16;
struct WindowBatchRequest {
  std::array<WindowRequest, LibraryBatchRequests> windows{};
  uint32_t bytes = LibraryBytes;
  uint16_t rows = LibraryRows;
  uint8_t count = 0;
  uint16_t statsWidgets = 0;
  uint8_t personalWidgets = 0;
  uint32_t personalPick = 0;
};
struct WindowSpan {
  WindowRequest request{};
  uint32_t total = 0, bytes = 0;
  uint16_t first = 0, count = 0;
  Availability availability = Availability::NotSaved;
};
struct BookRow {
  BookKey key{};
  std::array<uint8_t, 16> book{}, edition{};
  std::array<uint8_t, 32> revision{};
  std::array<char, 96> title{};
  uint32_t page = 0, pages = 0, seconds = 0;
  bool identityKnown = false, readingKnown = false, statsKnown = false, titleTruncated = false;
};
enum class FileAvailability : uint8_t { Unknown, OnCard, ServerOnly };
struct LibraryWindow {
  ContinueValues resume{};
  uint64_t publication = 0;  // Non-reused in-process content revision, independent of catalog/capture IDs.
  Provenance provenance{};
  WindowRequest request{};
  std::array<BookRow, LibraryRows> rows{};
  // File truth belongs to this published window, independently of identity/cover.
  std::array<FileAvailability, LibraryRows> fileAvailability{};
  std::array<WindowSpan, LibraryBatchRequests> windows{};
  uint8_t windowCount = 0;
  uint32_t total = 0, bytes = 0;
  uint16_t count = 0;
  Availability availability = Availability::NotSaved;
  bool stale = false;
};
struct Subscription {
  uint64_t sequence = 0, lifetime = 0;
  bool live = false;
};
static_assert(sizeof(LibraryWindow) <= LibraryBytes);
}  // namespace booksync
