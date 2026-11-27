// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/ui/WidgetHost.h"
namespace booksync::widgets {
struct Shelf {
  static WindowRequest request(WindowSource source);
  static auto shelfCell(WidgetView viewValue, uint8_t style, WidgetBox itemBox, uint8_t itemIndex, WidgetCell& itemCell)
      -> bool;
  static WidgetView view(WidgetView viewValue);
};
}  // namespace booksync::widgets
