// SPDX-License-Identifier: GPL-3.0-or-later
#include "Week.h"
namespace booksync {
void projectWeek(const StatsSnapshot& stats, const StatsProjection& options, StatsCard& out) {
  if (!startStatsCard(options, stats.daysCoverage, out)) return;
  const auto weekday = (stats.firstDay + ReadingStats::Days - 1 + 3) % 7;
  out.currentDay = uint8_t(weekday < 0 ? weekday + 7 : weekday);
  out.firstWeekday = 1;  // Monday, matching the companion provider.
  bool any = false, all = true;
  for (unsigned i = 0; i < 7; ++i) {
    if (i > out.currentDay) continue;  // Future is blank, never an unknown/zero saved day.
    const auto day = stats.days[ReadingStats::Days - 1 - out.currentDay + i];
    out.days[i] = statsMetric(day);
    all &= day.known;
    if (!day.known) continue;
    any = true;
    if (day.value > UINT64_MAX - out.value.value) {
      out.state = StatsCardState::Oversize;
      return;
    }
    out.value.value += day.value;
  }
  out.hours = out.value.value / 3600;
  out.minutes = uint8_t(out.value.value / 60 % 60);
  out.value.known = any;
  out.complete &= all;
  out.state = !any ? StatsCardState::NotSaved : all && !out.value.value ? StatsCardState::Empty : StatsCardState::Ready;
}
}  // namespace booksync
