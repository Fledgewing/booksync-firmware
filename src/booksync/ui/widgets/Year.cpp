// SPDX-License-Identifier: GPL-3.0-or-later
#include "booksync/ui/widgets/Year.h"

namespace booksync::widgets {
bool Year::heatCell(const StatsCard& statsValue, const StatsYear& heat, WidgetBox box, unsigned index, StatsMark& out) {
  out = {};
  if (boundedStatsCard(statsValue) != StatsCardState::Ready || box.textOnly || box.width < 240 || box.height < 80 ||
      heat.count > 366 || index >= heat.count || heat.firstWeekday > 6 || heat.marks[index] > 6)
    return false;
  const unsigned offset = heat.firstWeekday + index;
  const bool expanded = box.height >= 180;
  const int left = expanded ? 12 : box.width * 96 / 456, top = expanded ? 110 : 46;
  const int pitchX = (box.width - left - 12) / 54;
  const int pitchY = (box.height - top - (expanded ? box.line + 8 : 8)) / 7;
  if (pitchX < 2 || pitchY < 2) return false;
  const int pitch = pitchX < pitchY ? pitchX : pitchY;
  const int size = pitch > 5 ? 4 : pitch - 1;
  const auto level = heat.marks[index];
  out = {left + int(offset / 7) * (box.width - left - 12) / 54,
         top + int(offset % 7) * pitchY,
         size,
         size,
         level != 0,
         level >= 2 && level <= 5,
         false,
         level == 6};
  return true;
}
}  // namespace booksync::widgets
