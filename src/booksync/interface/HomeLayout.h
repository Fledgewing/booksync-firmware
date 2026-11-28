// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/Models.h"
#include "booksync/interface/StatsWidgets.h"
namespace booksync {
constexpr uint8_t HomeWidgets = LibraryBatchRequests;
enum class WidgetKind : uint8_t {
  Continue,
  Shelf,
  Browse,
  Week,
  Streak,
  Year,
  Monthly,
  Today,
  Pages,
  When,
  Records,
  Goal,
  Forecast,
  Count
};
constexpr bool isStatsWidget(WidgetKind kind) {
  return kind == WidgetKind::Week || kind == WidgetKind::Streak || kind == WidgetKind::Year ||
         kind == WidgetKind::Monthly || kind == WidgetKind::Today || kind == WidgetKind::Pages ||
         kind == WidgetKind::When || kind == WidgetKind::Records || kind == WidgetKind::Goal ||
         kind == WidgetKind::Forecast;
}
constexpr StatsWidget statsWidgetKind(WidgetKind kind) {
  return kind == WidgetKind::Week      ? StatsWidget::Week
         : kind == WidgetKind::Streak  ? StatsWidget::Streak
         : kind == WidgetKind::Year    ? StatsWidget::Year
         : kind == WidgetKind::Monthly ? StatsWidget::Monthly
         : kind == WidgetKind::Today   ? StatsWidget::Today
         : kind == WidgetKind::Pages   ? StatsWidget::Pages
         : kind == WidgetKind::When    ? StatsWidget::When
         : kind == WidgetKind::Records ? StatsWidget::Records
         : kind == WidgetKind::Goal    ? StatsWidget::Goal
                                       : StatsWidget::Forecast;
}
struct WidgetPlacement {
  WidgetKind kind = WidgetKind::Continue;
  WindowSource source = WindowSource::Recent;
  uint8_t x = 0, y = 0, w = 2, h = 2, style = 0;
};
// Worker-projected installed widgets only. Unknown instances remain in storage.
struct HomePage {
  std::array<WidgetPlacement, HomeWidgets> widgets{};
  std::array<uint16_t, 12> reserved{};  // Unknown visible instances still own their cells.
  std::array<char, 81> title{};         // User-provided page name; empty uses the translated Home label.
  uint32_t revision = 0;
  bool textOnly = false;
  uint8_t page = 0, pages = 1, cols = 2, rows = 7, count = 0;
};
}  // namespace booksync
