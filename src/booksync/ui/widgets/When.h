// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/ui/widgets/StatsDrawing.h"
namespace booksync::widgets {
class When {
 public:
  static auto hourColumn(const StatsCard&, const StatsHours&, WidgetBox, unsigned, StatsMark&) -> bool;
};
}  // namespace booksync::widgets
