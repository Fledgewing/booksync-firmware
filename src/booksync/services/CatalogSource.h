// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string_view>

#include "DeviceIndex.h"
namespace booksync {
// Worker values: aliases are opaque and scoped by the caller's account ticket.
// Transfer hints are untrusted references, never verified local paths or
// canonical identity/revision evidence. No wire DTO reaches the Library/UI.
struct CatalogFile {
  std::array<char, 64> alias{}, version{};
  std::array<char, 65> digest{};
  std::array<char, 16> format{};
  std::array<char, 513> destination{}, acquisition{};
  std::array<char, 24> rendition{}, requestedRendition{};
  std::array<char, 513> selection{};
  bool sourceAudio = false, playableAudio = false;
  uint64_t bytes = 0;
  bool sizeKnown = false;
};
enum class CatalogReading : uint8_t { Unknown, Unstarted, InProgress, Finished };
struct CatalogRow {
  std::array<char, 64> alias{};
  std::array<char, 96> title{}, series{};
  std::array<char, 32> sequence{};
  std::array<std::array<char, 96>, 8> authors{};
  std::array<std::array<char, 16>, 8> formats{};
  std::array<CatalogFile, 4> files{};
  uint8_t authorCount = 0, formatCount = 0, fileCount = 0;
  std::array<char, 64> seriesAlias{};
  std::array<char, 40> added{}, updated{}, lastOpened{};
  std::array<char, 513> artwork{}, detail{};
  double fraction = 0;
  CatalogReading reading = CatalogReading::Unknown;
  bool fractionKnown = false, artworkKnown = false, artworkPresent = false;
  bool titleTruncated = false;
};
enum class CatalogStep : uint8_t { Record, End, Error };
enum class CatalogError : uint8_t { None, Unavailable, PermissionLost, Invalid, Oversized, RestartRequired, Canceled };
class CatalogSource {
 public:
  virtual ~CatalogSource() = default;
  virtual CatalogStep next(CatalogRow&) = 0;
  virtual CatalogError error() const = 0;
  virtual void cancel() = 0;
};
// F21 owns scoped alias persistence and independent identity/path resolution.
// This bridge feeds F13 without inventing paths or IDs from server hints. The
// binder must return Error for unresolved records, never silently omit a book.
// Its reused row lives with the worker, not on a constrained task's stack.
class CatalogIndexSource {
 public:
  using Bind = bool (*)(void*, const CatalogRow&, storage::IndexRecord&);
  CatalogIndexSource(CatalogSource& source, Bind bind, void* context)
      : source_(source), bind_(bind), context_(context) {}
  IndexSource source() { return {next, this}; }

 private:
  CatalogSource& source_;
  Bind bind_;
  void* context_;
  CatalogRow row_{};
  static ScanStep next(void*, storage::IndexRecord&);
};
}  // namespace booksync
