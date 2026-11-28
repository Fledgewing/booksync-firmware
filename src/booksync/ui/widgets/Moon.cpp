// SPDX-License-Identifier: GPL-3.0-or-later
#include "booksync/ui/widgets/Moon.h"
namespace booksync::widgets {
bool Moon::layoutTime(const MoonWidget& timeValue, WidgetBox box, TimeDrawing& out) {
  auto status = timeValue.status;
  status.state = boundedMoonCard(timeValue);
  if (!timeFrame(status, sizeof(timeValue), box, out)) return false;
  if (status.state != TimeCardState::Ready) return true;
  out.moon = {10, 32, 54, 54};
  timeLine(out, TimeLine::TimePhase, 76, 30, box.width - 86, 15);
  timeLine(out, TimeLine::Illumination, 76, 46, box.width - 86, 28);
  timeLine(out, TimeLine::Next, 76, timeValue.status.stale ? 74 : 78, box.width - 86, 12);
  if (timeValue.status.stale) timeLine(out, TimeLine::Stale, 10, box.height - 14, box.width - 20, 14);
  return true;
}
}  // namespace booksync::widgets
