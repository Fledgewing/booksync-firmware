// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/ui/widgets/StatsDrawing.h"
namespace booksync::widgets {
class Year {
 public:
  static auto heatCell(const StatsCard&, const StatsYear&, WidgetBox, unsigned, StatsMark&) -> bool;
};
}  // namespace booksync::widgets
