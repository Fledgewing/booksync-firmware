// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/ui/widgets/StatsDrawing.h"
namespace booksync::widgets {
class Monthly {
 public:
  static auto monthColumn(const StatsCard&, const StatsMonths&, WidgetBox, unsigned, StatsMark&) -> bool;
};
}  // namespace booksync::widgets
