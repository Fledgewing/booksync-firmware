// SPDX-License-Identifier: GPL-3.0-or-later
#include "Calendar.h"
#include "CalendarDates.h"
namespace booksync {
void projectMonthly(const StatsSnapshot& stats, const StatsProjection& options, StatsCard& out, StatsMonths& months) {
  for (auto& month : months.totals) month = {};
  months.year = months.currentMonth = 0;
  if (!startStatsCard(options, stats.daysCoverage, out)) return;
  StatsDates dates;
  if (!statsDates(stats, dates)) {
    out.state = StatsCardState::Oversize;
    return;
  }
  months.year = dates.year;
  months.currentMonth = dates.month;
  bool all = true;
  using namespace std::chrono;
  for (unsigned i = 0; i < ReadingStats::Days; ++i) {
    const auto day = stats.firstDay + i;
    const year_month_day date{sys_days{days{day}}};
    const auto value = stats.days[i];
    const unsigned month = unsigned(date.month()) - 1;
    // Previous month remains meaningful in January; it belongs to last year.
    const bool previous = dates.month ? unsigned(int(date.year())) == dates.year && month + 1 == dates.month
                                      : unsigned(int(date.year())) + 1 == dates.year && month == 11;
    if (previous && !statsAccumulate(out.best, value)) {
      out.state = StatsCardState::Oversize;
      return;
    }
    if (unsigned(int(date.year())) != dates.year) continue;
    all &= value.known;
    if (!statsAccumulate(months.totals[month], value) || !statsAccumulate(out.value, value)) {
      out.state = StatsCardState::Oversize;
      return;
    }
  }
  // Dates earlier than the rolling F39 window remain unknown, including Jan 1
  // on Dec 31 of a leap year. Never infer complete-year coverage from a partial window.
  all &= dates.first >= stats.firstDay;
  out.complete &= all;
  out.hours = out.value.value / 3600;
  out.minutes = uint8_t(out.value.value / 60 % 60);
  out.state = !out.value.known          ? StatsCardState::NotSaved
              : all && !out.value.value ? StatsCardState::Empty
                                        : StatsCardState::Ready;
}
}  // namespace booksync
