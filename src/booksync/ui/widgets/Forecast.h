// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/ui/widgets/StatsSummary.h"
namespace booksync::widgets {
class Forecast {
 public:
  static auto summaryPositions(const StatsCard&, WidgetBox, StatsSummary&) -> bool;
};
}  // namespace booksync::widgets
