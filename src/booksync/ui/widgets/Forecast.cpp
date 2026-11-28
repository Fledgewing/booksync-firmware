// SPDX-License-Identifier: GPL-3.0-or-later
#include "booksync/ui/widgets/Forecast.h"
namespace booksync::widgets {
bool Forecast::summaryPositions(const StatsCard& value, WidgetBox box, StatsSummary& out) {
  out = {};
  if (boundedStatsCard(value) != StatsCardState::Ready || box.textOnly || box.width < 120 || box.height < 80)
    return false;
  out = {box.height * 23 / 108, box.height * 57 / 108, box.height * 90 / 108,
         box.height * (value.stale ? 73 : 85) / 108};
  return true;
}
}  // namespace booksync::widgets
