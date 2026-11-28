// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "FilePublish.h"
#include "Identity.h"

namespace booksync::storage {
// Borrowed raw source locator. Unknown kind/version bytes are retained, never
// replayed by this store. Geometry is supplied separately by the native reader.
struct PositionValue {
  bool present = false, percentagePresent = false;
  double percentage = 0;
  uint64_t at = 0;
  uint16_t kind = 0, version = 0;
  std::string_view raw, label;
  bool reset = false, redated = false;
  bool browser = false;  // Normalized origin provenance, retained independently of reset.
  bool operator==(const PositionValue&) const = default;
};
struct PositionRecord {
  ScopeView scope;
  SourceIdentity source;
  uint64_t token = 0, declinedAt = 0, rereadAt = 0, lastOverrideAt = 0;
  bool declinedReset = false, pending = false, answerable = false, allowPush = false, compared = false,
       rereadReleased = false;
  PositionValue remote, marker, overridePlace;
};
enum class PositionLoad { Ready, Missing, Conflict, Unreadable };
// One file per exact account/source binding. Paths are caller-owned; never derive
// authority from a path hash. Serialized on the existing sync worker, never paint.
// Caller supplies reusable scratch (>= MaxBytes); returned views borrow it until
// the next load. save() streams borrowed values without allocating or copying a
// whole record onto the task stack. Damaged/unknown files remain untouched.
class PositionStore {
 public:
  static constexpr size_t MaxRaw = 4096, MaxLabel = 100, MaxScope = 128;
  static constexpr size_t MaxBytes = 16 * 1024;
  explicit PositionStore(PublishPaths paths) : paths_(paths) {}
  PositionLoad load(ScopeView, const SourceIdentity&, PositionRecord&, char* scratch, size_t capacity);
  bool save(const PositionRecord&);

 private:
  PublishPaths paths_;
  static bool validateFile(FsFile&, void*);
};
}  // namespace booksync::storage
