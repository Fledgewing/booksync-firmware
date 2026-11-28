// SPDX-License-Identifier: GPL-3.0-or-later
#include "booksync/ui/widgets/Records.h"
namespace booksync::widgets {
bool Records::recordCell(const StatsCard& statsValue, WidgetBox box, unsigned index, StatsMark& out) {
  out = {};
  if (boundedStatsCard(statsValue) != StatsCardState::Ready || box.textOnly || box.width < 240 || box.height < 80 ||
      index >= 5)
    return false;
  out = {10 + int(index) * (box.width - 12) / 5,
         box.height * 26 / 108,
         (box.width - 52) / 5,
         box.height * 45 / 108,
         true,
         false,
         false,
         false};
  return true;
}
}  // namespace booksync::widgets
