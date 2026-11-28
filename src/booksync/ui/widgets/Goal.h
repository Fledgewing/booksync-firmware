// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/ui/widgets/StatsSummary.h"
namespace booksync::widgets {
class Goal {
 public:
  static auto progressBar(const StatsCard&, const StatsGoal&, WidgetBox, StatsMark&) -> bool;
};
}  // namespace booksync::widgets
