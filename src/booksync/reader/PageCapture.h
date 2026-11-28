// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/storage/PageJournal.h"
namespace booksync::reader {
class PageCapture;
// Runtime-owned, RenderLock-serialized pending values. This queue outlives every
// reader, so failed SD writes survive activity exit without a heap allocation.
class PageCaptureQueue {
 public:
  static constexpr unsigned Pending = 8;
  storage::JournalResult flush(storage::PageJournal& journal);
  unsigned pending() const { return count_; }
  bool hasWork() const { return count_ || gap_; }

 private:
  friend class PageCapture;
  void lost(const storage::PageEvent& event);
  storage::PageEvent queue_[Pending]{}, gapEvent_{};
  unsigned count_ = 0;
  bool gap_ = false;
};
// RenderLock protects fixed values. start/accept never touch SD, allocate,
// inspect a document or alter the native timer. Origin/ordinal belong to the
// reader; the borrowed pending queue must outlive it. Flush is main-loop only.
class PageCapture {
 public:
  static constexpr unsigned Pending = PageCaptureQueue::Pending;
  explicit PageCapture(PageCaptureQueue& queue) : queue_(queue) {}
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
  storage::JournalResult flush(storage::PageJournal& journal) { return queue_.flush(journal); }
  unsigned pending() const { return queue_.pending(); }
  bool hasWork() const { return queue_.hasWork(); }

 private:
  PageCaptureQueue& queue_;
  storage::PageEvent origin_{};
  uint32_t ordinal_ = 0;
};
}  // namespace booksync::reader
