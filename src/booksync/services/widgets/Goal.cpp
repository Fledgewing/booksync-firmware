// SPDX-License-Identifier: GPL-3.0-or-later
#include "Goal.h"

#include "CalendarDates.h"
namespace booksync {
void projectGoal(const StatsSnapshot& stats, const StatsProjection& options, StatsCard& out, StatsGoal& goal) {
  goal = {};
  if (!startStatsCard(options, stats.goalCoverage, out)) return;
  const auto account = [&](const ReadingCoverage& field) {
    return field.saved && field.origin == ReadingOrigin::Account && options.provenance.owner &&
           field.scope.owner == options.provenance.owner && field.scope.epoch == options.provenance.accountEpoch;
  };
  StatsDates dates;
  if (!account(stats.goalCoverage) || !account(stats.finishedCoverage) || !stats.target.known ||
      !stats.finished.known || !stats.year.known || !statsDates(stats, dates) || stats.year.value != dates.year) {
    out.state = StatsCardState::NotSaved;
    return;
  }
  out.value = statsMetric(stats.finished);
  out.best = statsMetric(stats.target);
  out.accountLinked = true;
  goal.year = uint16_t(dates.year);
  if (!stats.target.value) {
    out.state = StatsCardState::Empty;
    return;
  }
  constexpr uint64_t MaxSafe = UINT64_MAX / 36600;
  if (stats.target.value > MaxSafe || stats.finished.value > MaxSafe) {
    out.state = StatsCardState::Oversize;
    return;
  }
  const uint64_t remaining = stats.finished.value < stats.target.value ? stats.target.value - stats.finished.value : 0;
  const auto days = uint64_t(dates.next - dates.today), yearDays = uint64_t(dates.next - dates.first);
  const auto pace = (remaining * 70 + days - 1) / days;
  if (pace > UINT32_MAX) {
    out.state = StatsCardState::Oversize;
    return;
  }
  goal.paceTenths = uint32_t(pace);
  const auto expected = (stats.target.value * uint64_t(dates.today - dates.first + 1) + yearDays - 1) / yearDays;
  goal.ahead = stats.finished.value >= expected;
  goal.difference = goal.ahead ? stats.finished.value - expected : expected - stats.finished.value;
  goal.percent =
      stats.finished.value >= stats.target.value ? 100 : uint8_t(stats.finished.value * 100 / stats.target.value);
  out.state = StatsCardState::Ready;
}
}  // namespace booksync
