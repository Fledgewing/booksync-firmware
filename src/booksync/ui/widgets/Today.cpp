// SPDX-License-Identifier: GPL-3.0-or-later
#include "booksync/ui/widgets/Today.h"
namespace booksync::widgets {
bool Today::summaryPositions(const StatsCard& statsValue, WidgetBox box, StatsSummary& out) {
  out = {};
  if (boundedStatsCard(statsValue) != StatsCardState::Ready || box.textOnly || box.width < 120 || box.height < 80)
    return false;
  out = {box.height * 20 / 108, box.height * 52 / 108, box.height * 85 / 108,
         box.height * (statsValue.stale ? 66 : 85) / 108};
  return true;
}
}  // namespace booksync::widgets
