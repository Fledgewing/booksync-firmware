// SPDX-License-Identifier: GPL-3.0-or-later
#include "booksync/ui/widgets/Monthly.h"
namespace booksync::widgets {
bool Monthly::monthColumn(const StatsCard& statsValue, const StatsMonths& months, WidgetBox box, unsigned index,
                          StatsMark& out) {
  out = {};
  if (boundedStatsCard(statsValue) != StatsCardState::Ready || box.textOnly || box.width < 240 || box.height < 80 ||
      months.currentMonth > 11 || index >= 12)
    return false;
  uint64_t peak = 1;
  for (const auto& month : months.totals)
    if (month.known && month.value > peak) peak = month.value;
  const int left = box.width * 110 / 456, pitch = (box.width - left - 12) / 12;
  const int barHeight = box.height / 4, chartBottom = box.height - box.line * 2 - 8;
  const auto value = months.totals[index];
  const int height = value.known ? int(value.value == peak ? barHeight : value.value / (peak / barHeight + 1)) : 0;
  out = {left + int(index) * (box.width - left - 12) / 12,
         chartBottom - (height ? height : 1),
         pitch - 3,
         height ? height : 1,
         value.known,
         value.value != 0,
         index == months.currentMonth,
         false};
  return true;
}
}  // namespace booksync::widgets
