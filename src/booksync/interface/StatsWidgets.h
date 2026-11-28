// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Models.h"
namespace booksync {
enum class StatsWidget : uint8_t { Week, Streak, Year, Monthly, Today, Pages, When, Records, Count };
constexpr unsigned StatsWidgetCount = unsigned(StatsWidget::Count);
enum class StatsCardState : uint8_t { NotSaved, Ready, Empty, Off, Unsupported, Unavailable, Oversize };
enum class StatsCardSource : uint8_t { Unknown, Reader, Account };
struct StatsMetric {
  uint64_t value = 0;
  bool known = false;
};
struct StatsCard {
  std::array<StatsMetric, 7> days{};
  StatsMetric value{}, best{}, reader{}, account{};
  Provenance provenance{};
  uint64_t savedAt = 0, hours = 0;
  uint32_t ageDays = 0;
  uint32_t bytes = sizeof(StatsCard);
  StatsCardState state = StatsCardState::NotSaved;
  StatsCardSource source = StatsCardSource::Unknown;
  uint8_t firstWeekday = 0, currentDay = 6, minutes = 0;
  bool stale = false, complete = false, accountLinked = false;
  void clear() {
    for (unsigned i = 0; i < days.size(); ++i) days[i] = {};
    value = best = reader = account = {};
    provenance = {};
    savedAt = hours = ageDays = firstWeekday = minutes = 0;
    currentDay = 6;
    bytes = sizeof(StatsCard);
    state = StatsCardState::NotSaved;
    source = StatsCardSource::Unknown;
    stale = complete = accountLinked = false;
  }
};
// Single shared calendar payload per visible batch, never per widget instance.
// Day marks: 0 unknown, 1 known zero, 2 reading, 6 future.
struct StatsYear {
  std::array<uint8_t, 366> marks{};
  std::array<uint16_t, 12> monthStarts{};
  uint16_t year = 0, count = 0, unknown = 0;
  uint8_t firstWeekday = 0;
};
struct StatsMonths {
  std::array<StatsMetric, 12> totals{};
  uint16_t year = 0;
  uint8_t currentMonth = 0;
};
struct StatsHours {
  std::array<StatsMetric, 24> totals{};
  uint8_t peakHour = 0, after18 = 0;
  bool peakKnown = false;
};
struct StatsRecords {
  std::array<StatsMetric, 5> values{};  // visits, session seconds, best-day visits, books, streak.
  std::array<StatsCardSource, 5> sources{};
};
struct StatsWidgetBatch {
  std::array<StatsCard, StatsWidgetCount> cards{};
  StatsYear year{};
  StatsMonths months{};
  StatsHours hours{};
  StatsRecords records{};
  uint16_t requested = 0;
};
constexpr uint16_t statsWidgetBit(StatsWidget id) { return 1u << unsigned(id); }
static_assert(sizeof(StatsCard) <= 256);
}  // namespace booksync
