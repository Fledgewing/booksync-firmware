// SPDX-License-Identifier: GPL-3.0-or-later
#include "booksync/ui/widgets/When.h"
namespace booksync::widgets {
bool When::hourColumn(const StatsCard& statsValue, const StatsHours& hours, WidgetBox box, unsigned index,
                      StatsMark& out) {
  out = {};
  if (boundedStatsCard(statsValue) != StatsCardState::Ready || box.textOnly || box.width < 120 || box.height < 80 ||
      index >= 24)
    return false;
  uint64_t peak = 1;
  for (const auto& hour : hours.totals)
    if (hour.known && hour.value > peak) peak = hour.value;
  const int chartHeight = box.height * (statsValue.stale ? 19 : 30) / 108;
  const int bottom = box.height * (statsValue.stale ? 70 : 87) / 108;
  const auto value = hours.totals[index];
  const int height = value.known ? int(value.value == peak ? chartHeight : value.value / (peak / chartHeight + 1)) : 0;
  const int left = 10 + int(index) * (box.width - 20) / 24;
  out = {left,
         bottom - (height ? height : 1),
         ((box.width - 20) / 24 > 2 ? (box.width - 20) / 24 - 2 : 1),
         height ? height : 1,
         value.known,
         value.value != 0,
         hours.peakKnown && index == hours.peakHour,
         false};
  return true;
}
}  // namespace booksync::widgets
