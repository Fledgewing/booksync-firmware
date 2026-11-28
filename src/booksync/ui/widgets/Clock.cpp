// SPDX-License-Identifier: GPL-3.0-or-later
#include "booksync/ui/widgets/Clock.h"
namespace booksync::widgets {
bool Clock::layoutTime(const ClockWidget& timeValue, WidgetBox box, TimeDrawing& out) {
  auto status = timeValue.status;
  status.state = boundedClockCard(timeValue);
  if (!timeFrame(status, sizeof(timeValue), box, out)) return false;
  if (status.state != TimeCardState::Ready) return true;
  timeLine(out, TimeLine::Value, 10, 24, box.width - 20, 34);
  timeLine(out, TimeLine::Date, 10, 61, box.width - 20, 18);
  timeLine(out, timeValue.status.stale ? TimeLine::Stale : TimeLine::TimeSource, 10, box.height - 22, box.width - 20,
           16);
  return true;
}
}  // namespace booksync::widgets
