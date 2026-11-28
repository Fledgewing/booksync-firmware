// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/ui/widgets/TimeDrawing.h"
namespace booksync::widgets {
struct Moon {
  static WidgetCell shadowRow(const MoonWidget&, int16_t radius, int16_t y);
  static auto layoutTime(const MoonWidget&, WidgetBox, TimeDrawing&) -> bool;
};
}  // namespace booksync::widgets
