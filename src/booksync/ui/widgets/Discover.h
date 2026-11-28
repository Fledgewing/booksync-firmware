// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/ui/WidgetHost.h"
#include "booksync/ui/widgets/Canvas.h"
namespace booksync::widgets {
struct Discover {
  static WindowRequest request();
  static auto paint(Canvas&, WidgetView, WidgetBox, uint8_t focusIndex) -> void;
};
}  // namespace booksync::widgets
