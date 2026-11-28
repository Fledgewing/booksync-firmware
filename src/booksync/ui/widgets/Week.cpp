// SPDX-License-Identifier: GPL-3.0-or-later
#include "booksync/ui/widgets/Week.h"
namespace booksync::widgets {
bool Week::layoutStats(const StatsCard& statsValue, WidgetBox box, StatsDrawing& out) {
  out = {};
  if (box.textOnly || box.width < 120 || box.height < 80 || boundedStatsCard(statsValue) != StatsCardState::Ready)
    return false;
  uint64_t max = 1;
  for (const auto& day : statsValue.days)
    if (day.known && day.value > max) max = day.value;
  const int chartBottom = box.height * 85 / 108;
  const int height = box.height * (statsValue.stale || !statsValue.complete ? 20 : 33) / 108;
  const int columnWidth = (box.width - 20) / 7;
  for (unsigned i = 0; i < 7; ++i) {
    // Divide before multiplication: even hostile UINT64_MAX values stay bounded.
    const auto& day = statsValue.days[i];
    const int h = day.known ? int(day.value == max ? height : day.value / (max / height + 1)) : 0;
    out.marks[i] = {12 + int(i) * (box.width - 20) / 7,
                    chartBottom - (h ? h : 1),
                    columnWidth - 4,
                    h ? h : 1,
                    day.known,
                    day.value != 0,
                    i == statsValue.currentDay,
                    i > statsValue.currentDay};
  }
  return true;
}
}  // namespace booksync::widgets
