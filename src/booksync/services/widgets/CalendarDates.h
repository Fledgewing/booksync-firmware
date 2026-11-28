// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <chrono>

#include "StatsProjection.h"
namespace booksync {
struct StatsDates {
  int64_t today = 0, first = 0, next = 0;
  unsigned year = 0, month = 0, weekday = 0;
};
inline bool statsDates(const StatsSnapshot& stats, StatsDates& out) {
  using namespace std::chrono;
  if (stats.firstDay < 0 || stats.firstDay > 2932500) return false;
  out.today = stats.firstDay + ReadingStats::Days - 1;
  const year_month_day date{sys_days{days{out.today}}};
  if (!date.ok()) return false;
  out.year = unsigned(int(date.year()));
  out.month = unsigned(date.month()) - 1;
  out.first = sys_days{date.year() / January / 1}.time_since_epoch().count();
  out.next = sys_days{(date.year() + years{1}) / January / 1}.time_since_epoch().count();
  out.weekday = unsigned((out.first + 3) % 7);
  return true;
}
inline ReadingValue statsDateValue(const StatsSnapshot& stats, int64_t day) {
  const auto index = day - stats.firstDay;
  return index >= 0 && index < ReadingStats::Days ? stats.days[unsigned(index)] : ReadingValue{};
}
inline bool statsAccumulate(StatsMetric& metric, ReadingValue value) {
  if (!value.known) return true;
  if (value.value > UINT64_MAX - metric.value) return false;
  metric.known = true;
  metric.value += value.value;
  return true;
}
}  // namespace booksync
