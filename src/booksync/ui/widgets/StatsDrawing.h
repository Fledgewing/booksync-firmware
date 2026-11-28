// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/StatsWidgets.h"
#include "booksync/ui/WidgetHost.h"
namespace booksync::widgets {
struct StatsMark {
  int x = 0, y = 0, width = 0, height = 0;
  bool known = false, active = false, latestDay = false, future = false;
};
struct StatsDrawing {
  std::array<StatsMark, 7> marks{};
  bool dots = false;
};
static_assert(sizeof(StatsDrawing) <= 160);
inline StatsCardState boundedStatsCard(const StatsCard& statsValue) {
  if (statsValue.bytes > sizeof(StatsCard)) return StatsCardState::Oversize;
  if (statsValue.bytes != sizeof(StatsCard)) return StatsCardState::Unavailable;
  return statsValue.state;
}
}  // namespace booksync::widgets
