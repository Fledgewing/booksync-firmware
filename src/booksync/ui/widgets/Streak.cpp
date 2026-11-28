// SPDX-License-Identifier: GPL-3.0-or-later
#include "booksync/ui/widgets/Streak.h"
namespace booksync::widgets {
bool Streak::layoutStats(const StatsCard& statsValue, WidgetBox box, StatsDrawing& out) {
  out = {};
  out.dots = true;
  if (box.textOnly || box.width < 120 || box.height < 80 || boundedStatsCard(statsValue) != StatsCardState::Ready)
    return false;
  const int size = box.height * 10 / 108;
  for (unsigned i = 0; i < 7; ++i)
    out.marks[i] = {10 + int(i) * (box.width - 20 - size) / 6,
                    box.height * (statsValue.stale ? 60 : 74) / 108,
                    size,
                    size,
                    statsValue.days[i].known,
                    statsValue.days[i].value != 0,
                    i == 6};
  return true;
}
}  // namespace booksync::widgets
