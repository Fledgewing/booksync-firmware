// SPDX-License-Identifier: GPL-3.0-or-later
#include "Calendar.h"
#include "CalendarDates.h"
namespace booksync {
void projectYear(const StatsSnapshot& stats, const StatsProjection& options, StatsCard& out, StatsYear& heat) {
  heat.marks.fill(0);
  heat.monthStarts.fill(0);
  heat.year = heat.count = heat.unknown = heat.firstWeekday = 0;
  if (!startStatsCard(options, stats.daysCoverage, out)) return;
  StatsDates dates;
  if (!statsDates(stats, dates)) {
    out.state = StatsCardState::Oversize;
    return;
  }
  heat.year = dates.year;
  heat.count = uint16_t(dates.next - dates.first);
  heat.firstWeekday = dates.weekday;
  using namespace std::chrono;
  for (unsigned month = 0; month < 12; ++month)
    heat.monthStarts[month] = uint16_t(
        sys_days{year{int(dates.year)} / std::chrono::month{month + 1} / 1}.time_since_epoch().count() - dates.first);
  bool all = true;
  for (unsigned i = 0; i < heat.count; ++i) {
    const auto day = dates.first + i;
    if (day > dates.today) {
      heat.marks[i] = 6;
      continue;
    }
    const auto value = statsDateValue(stats, day);
    all &= value.known;
    if (!value.known) {
      ++heat.unknown;
      continue;
    }
    if (!statsAccumulate(out.value, value)) {
      out.state = StatsCardState::Oversize;
      return;
    }
    out.best.known = true;
    out.best.value += value.value != 0;
    heat.marks[i] = value.value == 0 ? 1 : 2;
  }
  out.complete &= all;
  out.hours = out.value.value / 3600;
  out.minutes = uint8_t(out.value.value / 60 % 60);
  out.state = !out.value.known          ? StatsCardState::NotSaved
              : all && !out.value.value ? StatsCardState::Empty
                                        : StatsCardState::Ready;
}
}  // namespace booksync
