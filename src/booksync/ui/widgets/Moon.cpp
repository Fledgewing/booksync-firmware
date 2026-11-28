// SPDX-License-Identifier: GPL-3.0-or-later
#include "booksync/ui/widgets/Moon.h"
namespace booksync::widgets {
bool Moon::layoutTime(const MoonWidget& timeValue, WidgetBox box, TimeDrawing& out) {
  auto status = timeValue.status;
  status.state = boundedMoonCard(timeValue);
  if (!timeFrame(status, sizeof(timeValue), box, out)) return false;
  if (status.state != TimeCardState::Ready) return true;
  out.moon = {10, 28, 36, 36};
  timeLine(out, TimeLine::TimePhase, 54, 26, box.width - 64, 18);
  timeLine(out, TimeLine::Illumination, 54, 48, box.width - 64, 18);
  timeLine(out, TimeLine::Next, 10, 69, box.width - 20, 18);
  if (timeValue.status.stale) timeLine(out, TimeLine::Stale, 10, box.height - 18, box.width - 20, 14);
  return true;
}
}  // namespace booksync::widgets
