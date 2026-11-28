// SPDX-License-Identifier: GPL-3.0-or-later
#include "Streak.h"
namespace booksync {
void projectStreak(const StatsSnapshot& stats, const StatsProjection& options, StatsCard& out) {
  if (!startStatsCard(options, stats.streakCoverage, out)) return;
  out.value = statsMetric(stats.streak);
  out.best = statsMetric(stats.bestStreak);
  // Daily evidence retains its own precedence. Unknown days remain hollow.
  for (unsigned i = 0; i < 7; ++i) out.days[i] = statsMetric(stats.days[ReadingStats::Days - 7 + i]);
  bool observed = false;
  for (const auto& day : stats.days) observed |= day.known && day.value != 0;
  const bool empty = !out.value.value && out.best.known && !out.best.value && !observed;
  out.state = !out.value.known ? StatsCardState::NotSaved : empty ? StatsCardState::Empty : StatsCardState::Ready;
}
}  // namespace booksync
