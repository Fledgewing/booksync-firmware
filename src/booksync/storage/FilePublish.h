// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <HalStorage.h>

#include <cstddef>
#include <cstdint>

namespace booksync::storage {
#ifdef SIMULATOR
uint32_t publicationCheckpoints();
#endif
// Caller-owned, null-terminated paths, alive for the transaction. Reserve exactly
// <current>.tmp and <current>.bak in the same directory, on the same volume.
struct PublishPaths {
  const char* current;
  const char* staging;
  const char* backup;
};
enum class PublishedFile : uint8_t { Missing, Current, Backup, Unreadable };
// Must validate the entire format/length/integrity, with bounded memory. The
// handle is at offset zero; never retain it or open another reader for its path.
using FileValidator = bool (*)(FsFile&, void*);

// Worker-only primitive. One serialized writer must own these three paths, with
// readers excluded throughout publication. No tasks, locks or payload allocations
// are introduced here. Use select() on every restart; never open staging as data.
class FilePublish {
 public:
  FilePublish(PublishPaths paths, FileValidator validator, void* context = nullptr);
  ~FilePublish();
  FilePublish(const FilePublish&) = delete;
  FilePublish& operator=(const FilePublish&) = delete;
  bool begin(size_t expectedBytes);
  // Explicit resume only: caller has authenticated the exact selection/validator.
  // Staging length must equal received; no implicit truncation or publication.
  bool resume(size_t expectedBytes, size_t received);
  bool flush();       // Durable partial bytes while retaining the reusable handle.
  bool checkpoint();  // Close and durably retain partial staging; never publish it.
  bool write(const void* bytes, size_t count);
  // False may mean the new complete file is visible but its final directory sync
  // failed: reconcile with select(), rather than assuming a failed remote intent.
  bool finish();
  // Initial authority records must never replace/recover an existing state.
  bool finishNew();
  // Positive current-only validation: missing/read errors never select backup.
  static bool validateCurrent(PublishPaths paths, FileValidator validator, void* context = nullptr);
  static PublishedFile select(PublishPaths paths, FileValidator validator, void* context = nullptr);

 private:
  const PublishPaths paths_;
  const FileValidator validator_;
  void* const context_;
  FsFile file_;
  size_t remaining_ = 0;
  bool active_ = false;
  bool failed_ = false;
  bool fail(const char* step);
  bool finishImpl(bool replace);
};
}  // namespace booksync::storage
