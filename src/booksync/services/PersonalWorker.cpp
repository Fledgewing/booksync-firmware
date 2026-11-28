// SPDX-License-Identifier: GPL-3.0-or-later
#include "PersonalWorker.h"

#include "Clock.h"
namespace booksync {
void PersonalWorker::publish() { published_.publication = ++sequence_; }
void PersonalWorker::retire() {
  native_.retire();
  published_.clear();
  busy_ = selecting_ = false;
}
bool PersonalWorker::begin(uint8_t mask, uint32_t pick, ClockStatus clock) {
  retire();
  if (mask > 15 || pick > 1000000000) return false;
  published_.requested = mask;
  published_.highlight.status.bytes = sizeof(HighlightWidget);
  published_.yearAgo.status.bytes = sizeof(YearAgoWidget);
  published_.recommended.status.bytes = sizeof(RecommendedWidget);
  published_.discover.status.bytes = sizeof(DiscoverWidget);
  const auto validated = projectClock(clock.evidence, clock.utcSeconds, clock.local);
  if (!validated.hasDateTime() || validated.utcOffsetSeconds != clock.utcOffsetSeconds) {
    publish();
    return true;
  }
  published_.localDay = (validated.utcSeconds + validated.utcOffsetSeconds) / 86400;
  // Account history/recommendations require a saved, owner-verified provider.
  // The native source does not infer these from file absence or session totals.
  if (!(mask & personalWidgetBit(PersonalWidget::Highlight))) {
    publish();
    return true;
  }
  if (!native_.begin()) {
    published_.highlight.status.state = native_.availability(PersonalKind::Highlight) == PersonalState::Oversized
                                            ? PersonalCardState::Oversize
                                            : PersonalCardState::Unavailable;
    publish();
    return true;
  }
  busy_ = true;
  return true;
}
bool PersonalWorker::tick() {
  if (!busy_) return true;
  if (native_.busy()) {
    native_.tick();
    return true;
  }
  if (!selecting_) {
    selection_.begin(PersonalKind::Highlight, published_.localDay, 0, {});
    selecting_ = true;
  }
  selection_.tick(1);
  if (selection_.busy()) return true;
  const auto& selected = selection_.result();
  auto& out = published_.highlight;
  // Enum layouts deliberately differ (Oversized/Off); map explicitly.
  switch (selected.state) {
    case PersonalState::NotSaved:
      out.status.state = PersonalCardState::NotSaved;
      break;
    case PersonalState::Ready:
      out.status.state = PersonalCardState::Ready;
      break;
    case PersonalState::Empty:
      out.status.state = PersonalCardState::Empty;
      break;
    case PersonalState::Unsupported:
      out.status.state = PersonalCardState::Unsupported;
      break;
    case PersonalState::Unavailable:
      out.status.state = PersonalCardState::Unavailable;
      break;
    case PersonalState::Off:
      out.status.state = PersonalCardState::Off;
      break;
    case PersonalState::Oversized:
      out.status.state = PersonalCardState::Oversize;
      break;
  }
  out.status.total = selected.count;
  out.status.origin = selected.coverage.saved ? PersonalOrigin::ThisReader : PersonalOrigin::Unknown;
  if (selected.state == PersonalState::Ready) {
    out.book = selected.record.book;
    out.passage = selected.record.passage;
    out.source = selected.record.source;
    out.canOpen = true;
  }
  native_.retire();
  busy_ = selecting_ = false;
  publish();
  return true;
}
}  // namespace booksync
