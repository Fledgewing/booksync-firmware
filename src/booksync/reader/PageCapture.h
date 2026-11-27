// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/storage/PageJournal.h"
namespace booksync::reader {
// RenderLock protects these fixed values. start/accept never touch SD, allocate,
// inspect a document or alter the native timer. flush is main-loop/exit only.
class PageCapture {
 public:
  static constexpr unsigned Pending = 8;
  void start(storage::PageEvent origin) { origin_ = origin; }
  bool needsLocation(uint32_t spine, uint32_t page, uint32_t pages) const {
    return origin_.start && !origin_.scale && origin_.spine == spine && origin_.page == page && origin_.pages == pages;
  }
  void locate(uint32_t spine, uint32_t page, uint32_t pages, uint32_t fraction, uint32_t scale, uint32_t offset = 0,
              bool hasOffset = false) {
    if (origin_.spine == spine && origin_.page == page && origin_.pages == pages) {
      origin_.fraction = fraction;
      origin_.scale = scale;
      origin_.offset = offset;
      origin_.flags = hasOffset ? storage::PageEvent::HasOffset : 0;
    }
  }
  void accept(uint64_t end, uint32_t seconds);
  storage::JournalResult flush(storage::PageJournal& journal);
  unsigned pending() const { return count_; }
  bool hasWork() const { return count_ || gap_; }

 private:
  storage::PageEvent origin_{}, queue_[Pending]{};
  uint32_t ordinal_ = 0;
  unsigned count_ = 0;
  bool gap_ = false;
};
}  // namespace booksync::reader
