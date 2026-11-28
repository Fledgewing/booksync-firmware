// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <HalStorage.h>

#include "LookupPool.h"
namespace booksync {
// Read-only iterator over native cache folders. A cache hash is a locator only:
// V1 lookup history does not prove a book identity, context or timestamp.
class SavedLookupPoolSource {
 public:
  void begin();
  void retire() { live_ = false; }
  LookupPoolPorts ports() { return {this, next, valid}; }
  uint32_t consumedBytes() const { return bytes_; }

 private:
  static constexpr uint32_t MaxDirectories = 10000, MaxCaptureBytes = 16 * 1024 * 1024;
  LookupHistorySource history_{};
#ifdef SIMULATOR
  // Pinned POSIX HAL exposes its consumed-entry cookie through position/seekSet.
  size_t position_ = 0;
#else
  // Physical HAL also retains the FAT cluster, avoiding a directory-chain walk.
  FsFile::DirectoryPosition position_{};
#endif
  std::array<char, 96> cache_{};
  std::array<char, 64> name_{};
  uint32_t directories_ = 0, bytes_ = 0;
  bool live_ = false, positioned_ = false, reading_ = false, saved_ = false;
  static LookupStep next(void*, LookupEntry&);
  static bool valid(void* p) { return static_cast<SavedLookupPoolSource*>(p)->live_; }
  LookupStep advance(LookupEntry&);
};
static_assert(sizeof(SavedLookupPoolSource) <= 2304);
}  // namespace booksync
