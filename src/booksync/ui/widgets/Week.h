// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/ui/widgets/StatsDrawing.h"
namespace booksync::widgets {
struct Week {
  static auto layoutStats(const StatsCard&, WidgetBox, StatsDrawing&) -> bool;
};
}  // namespace booksync::widgets
