// SPDX-License-Identifier: GPL-3.0-or-later
#include "PageCapture.h"

#include <Logging.h>
namespace booksync::reader {
void PageCapture::accept(uint64_t end, uint32_t seconds) {
  if (!seconds) return;
  if (!origin_.open) {
    queue_.lost(origin_);
    return;
  }
  auto event = origin_;
  origin_.start = 0;  // An accepted native interval can be observed only once.
  if (!event.start || end < event.start || end - event.start + 2 < seconds ||
      end - event.start > uint64_t(seconds) + 2 || !event.scale || !event.pages || event.page >= event.pages ||
      event.fraction > event.scale) {
    queue_.lost(event);
    return;
  }
  if (queue_.count_ == Pending || ordinal_ == UINT32_MAX) {
    queue_.lost(event);
    return;
  }
  event.ordinal = ++ordinal_;
  event.seconds = seconds;
  queue_.queue_[queue_.count_++] = event;
}
void PageCaptureQueue::lost(const storage::PageEvent& event) {
  if (!gap_)
    gapEvent_ = {event.open, event.start, event.ordinal, 0, 0, 0, 0, 0, 0, 0, storage::PageEvent::Gap};
  else if (gapEvent_.open != event.open)
    gapEvent_.open = 0;  // Multiple sources: explicit global coverage gap.
  gap_ = true;
}
storage::JournalResult PageCaptureQueue::flush(storage::PageJournal& journal) {
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
    const auto result = journal.append(gapEvent_);
    if (result != storage::JournalResult::Ok) return result;
    gap_ = false;
  }
  return storage::JournalResult::Ok;
}
}  // namespace booksync::reader
