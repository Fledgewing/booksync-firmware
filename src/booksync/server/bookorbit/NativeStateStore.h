// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "LifecycleState.h"
#include "booksync/reader/Capture.h"

namespace booksync::server {
struct NativeStateDocument {
  storage::ScopeView scope;
  storage::SourceIdentity source;
  uint64_t token = 1;  // Native user-status version, not a remote request receipt.
  bool manualStatusPending = false;
  uint64_t entry = 0, nativeOpen = 0;  // Nonzero only in an immutable capture.
  std::string_view payload;
  StateValues local, history;
  std::string_view reviewModified;
  std::string_view document;
};
enum class NativeStateLoad : uint8_t { Ready, Missing, Conflict, Unreadable };
// New account/source-scoped state only; never reads or migrates old native
// queues. Native values and remote-known history are separate. Freeze uses
// create-only authority; save accepts only a current document and monotonic
// native token. Views borrow caller-owned >= MaxBytes scratch until next call.
class NativeStateStore final {
 public:
  static constexpr size_t MaxBytes = 24 * 1024;
  NativeStateStore(storage::PublishPaths paths, char* scratch, size_t capacity)
      : paths_(paths), scratch_(scratch), capacity_(capacity) {}
  NativeStateStore(const NativeStateStore&) = delete;
  NativeStateStore& operator=(const NativeStateStore&) = delete;
  NativeStateLoad load(storage::ScopeView, const storage::SourceIdentity&, NativeStateDocument&);
  bool loadCapture(storage::ScopeView, const storage::SourceIdentity&, uint64_t entry, uint64_t nativeOpen,
                   std::string_view payload, std::string_view document, NativeStateDocument&);
  bool freeze(const NativeStateDocument&);  // Inputs must be disjoint from scratch.
  bool save(const NativeStateDocument&);
  bool disjoint(const char* bytes, size_t size) const;
  bool disjoint(const NativeStateStore& other) const { return disjoint(other.scratch_, other.capacity_); }

 private:
  storage::PublishPaths paths_;
  char* scratch_;
  size_t capacity_;
  NativeStateDocument decoded_{};
  reader::OpenCapture opened_{};
  std::array<char, 129> server_{}, account_{};
  storage::SourceIdentity expected_{};
  std::array<char, 130> header_{};
  bool expect(storage::ScopeView, const storage::SourceIdentity&);
  bool bound() const;
  bool overlaps(std::string_view) const;
  bool valid(const NativeStateDocument&);
  bool decode(std::string_view);
  bool write(const NativeStateDocument&, bool createOnly);
  static bool validateFile(FsFile&, void*);
};
static_assert(sizeof(NativeStateStore) <= 1536);
}  // namespace booksync::server
