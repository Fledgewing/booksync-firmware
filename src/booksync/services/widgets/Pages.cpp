// SPDX-License-Identifier: GPL-3.0-or-later
#include "Pages.h"
namespace booksync {
void projectPages(const StatsSnapshot& stats, const StatsProjection& options, StatsCard& out) {
  if (!startStatsCard(options, stats.recordsCoverage, out)) return;
  out.value = statsMetric(stats.todayPages);
  out.best = statsMetric(stats.recordedPages);
  out.state = !out.value.known && !out.best.known                                           ? StatsCardState::NotSaved
              : out.best.known && !out.best.value && (!out.value.known || !out.value.value) ? StatsCardState::Empty
                                                                                            : StatsCardState::Ready;
}
}  // namespace booksync
