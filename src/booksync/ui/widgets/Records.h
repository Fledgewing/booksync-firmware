// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/ui/widgets/StatsDrawing.h"
namespace booksync::widgets {
class Records {
 public:
  static auto recordCell(const StatsCard&, WidgetBox, unsigned, StatsMark&) -> bool;
};
}  // namespace booksync::widgets
