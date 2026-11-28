// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/reader/Capture.h"
#include "booksync/storage/PositionStore.h"

namespace booksync::server {
struct FrozenProgress {
  storage::ScopeView scope;
  storage::SourceIdentity source;
  uint64_t entry = 0, nativeOpen = 0;
  std::string_view payload;
  storage::PositionValue value;
  bool deliver = false;
  bool wasOverride = false;
  std::string_view document;
};
// Private immutable per-delivery capture. Caller owns distinct stable paths and
// >= MaxBytes reusable worker scratch. Freeze inputs must not borrow that scratch;
// loaded views borrow it until the next call. Unknown native locator bytes remain
// intact. Only the later evidence provider interprets a supported protocol kind.
class ProgressCapture final {
 public:
  static constexpr size_t MaxBytes = 8192;
  ProgressCapture(storage::PublishPaths paths, char* scratch, size_t capacity)
      : paths_(paths), scratch_(scratch), capacity_(capacity) {}
  ProgressCapture(const ProgressCapture&) = delete;
  ProgressCapture& operator=(const ProgressCapture&) = delete;
  bool freeze(const FrozenProgress&);
  bool load(storage::ScopeView, const storage::SourceIdentity&, uint64_t entry, uint64_t nativeOpen,
            std::string_view payload, std::string_view document, FrozenProgress&);
  bool disjoint(const char* bytes, size_t size) const { return !bytes || !size || !overlaps({bytes, size}); }

 private:
  storage::PublishPaths paths_;
  char* scratch_;
  size_t capacity_;
  FrozenProgress decoded_{};
  reader::OpenCapture opened_{};
  std::array<char, 129> server_{}, account_{};
  bool decode(std::string_view);
  bool valid(const FrozenProgress&);
  bool overlaps(std::string_view) const;
  static bool validateFile(FsFile&, void*);
};
}  // namespace booksync::server
