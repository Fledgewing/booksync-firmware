// SPDX-License-Identifier: GPL-3.0-or-later
#include "PageCapture.h"

#include <Logging.h>
namespace booksync::reader {
void PageCapture::accept(uint64_t end, uint32_t seconds) {
  if (!seconds) return;
  if (!origin_.open) {
    gap_ = true;
    return;
  }
  auto event = origin_;
  origin_.start = 0;  // An accepted native interval can be observed only once.
  if (!event.start || end < event.start || end - event.start + 2 < seconds ||
      end - event.start > uint64_t(seconds) + 2 || !event.scale || !event.pages || event.page >= event.pages ||
      event.fraction > event.scale) {
    gap_ = true;
    return;
  }
  if (count_ == Pending || ordinal_ == UINT32_MAX) {
    gap_ = true;
    return;
  }
  event.ordinal = ++ordinal_;
  event.seconds = seconds;
  queue_[count_++] = event;
}
storage::JournalResult PageCapture::flush(storage::PageJournal& journal) {
  while (count_) {
    const auto result = journal.append(queue_[0]);
    if (result != storage::JournalResult::Ok) {
      LOG_ERR("BSPG", "Page capture pending or covered by permanent capacity gap");
      if (result == storage::JournalResult::Capacity && journal.capped()) {
        count_ = 0;
        gap_ = false;
      }
      return result;
    }
    --count_;
    for (unsigned i = 0; i < count_; ++i) queue_[i] = queue_[i + 1];
  }
  if (gap_) {
    auto marker = origin_;
    marker.flags = storage::PageEvent::Gap;
    marker.ordinal = ordinal_;
    const auto result = journal.append(marker);
    if (result != storage::JournalResult::Ok) return result;
    gap_ = false;
  }
  return storage::JournalResult::Ok;
}
}  // namespace booksync::reader
