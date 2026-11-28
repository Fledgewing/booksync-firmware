// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Models.h"
namespace booksync {
enum class StatsWidget : uint8_t { Week, Streak, Count };
constexpr unsigned StatsWidgetCount = unsigned(StatsWidget::Count);
enum class StatsCardState : uint8_t { NotSaved, Ready, Empty, Off, Unsupported, Unavailable, Oversize };
enum class StatsCardSource : uint8_t { Unknown, Reader, Account };
struct StatsMetric {
  uint64_t value = 0;
  bool known = false;
};
struct StatsCard {
  std::array<StatsMetric, 7> days{};
  StatsMetric value{}, best{};
  Provenance provenance{};
  uint64_t savedAt = 0, hours = 0;
  uint32_t ageDays = 0;
  uint32_t bytes = sizeof(StatsCard);
  StatsCardState state = StatsCardState::NotSaved;
  StatsCardSource source = StatsCardSource::Unknown;
  uint8_t firstWeekday = 0, currentDay = 6, minutes = 0;
  bool stale = false, complete = false;
  void clear() {
    for (unsigned i = 0; i < days.size(); ++i) days[i] = {};
    value = best = {};
    provenance = {};
    savedAt = hours = ageDays = firstWeekday = minutes = 0;
    currentDay = 6;
    bytes = sizeof(StatsCard);
    state = StatsCardState::NotSaved;
    source = StatsCardSource::Unknown;
    stale = complete = false;
  }
};
struct StatsWidgetBatch {
  std::array<StatsCard, StatsWidgetCount> cards{};
  uint16_t requested = 0;
};
constexpr uint16_t statsWidgetBit(StatsWidget id) { return 1u << unsigned(id); }
static_assert(sizeof(StatsCard) <= 256);
}  // namespace booksync
