// SPDX-License-Identifier: GPL-3.0-or-later
#include "Clock.h"
namespace booksync {
ClockStatus projectClock(ClockEvidence evidence, int64_t utcSeconds, ClockDateTime local) {
  if (evidence == ClockEvidence::Unknown || evidence > ClockEvidence::Retained || utcSeconds <= 0 ||
      local.year < 2024 || local.year > 2101 || !local.month || local.month > 12 || !local.day ||
      local.hour > 23 || local.minute > 59 || local.second > 59) return {};
  static constexpr uint8_t lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = local.year % 4 == 0 && (local.year % 100 != 0 || local.year % 400 == 0);
  const unsigned lastDay = lengths[local.month - 1] + (local.month == 2 && leap ? 1 : 0);
  if (local.day > lastDay) return {};
  // Gregorian civil date to epoch days, using complete 400-year eras.
  const int year = local.year - (local.month <= 2);
  const int era = year / 400;
  const unsigned y = year - era * 400;
  const unsigned m = local.month > 2 ? local.month - 3 : local.month + 9;
  const unsigned doy = (153 * m + 2) / 5 + local.day - 1;
  const int64_t days = era * 146097LL + y * 365 + y / 4 - y / 100 + doy - 719468;
  const int64_t wall = days * 86400 + local.hour * 3600 + local.minute * 60 + local.second;
  // Bound before subtracting an arbitrary caller epoch, avoiding signed overflow.
  if (utcSeconds < wall - 18 * 3600 || utcSeconds > wall + 18 * 3600) return {};
  local.weekday = (days + 4) % 7;  // Sunday=0, shared with the native calendar.
  return {evidence, utcSeconds, static_cast<int32_t>(wall - utcSeconds), local};
}
}  // namespace booksync
