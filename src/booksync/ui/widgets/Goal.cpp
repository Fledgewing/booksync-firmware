// SPDX-License-Identifier: GPL-3.0-or-later
#include "booksync/ui/widgets/Goal.h"
namespace booksync::widgets {
bool Goal::progressBar(const StatsCard& value, const StatsGoal& goal, WidgetBox box, StatsMark& out) {
  out = {};
  if (boundedStatsCard(value) != StatsCardState::Ready || box.textOnly || box.width < 120 || box.height < 80 ||
      goal.percent > 100)
    return false;
  out = {10,
         box.height * 58 / 108,
         (box.width - 20) * goal.percent / 100,
         box.height * 7 / 108,
         true,
         goal.percent != 0,
         false,
         false};
  return true;
}
}  // namespace booksync::widgets
