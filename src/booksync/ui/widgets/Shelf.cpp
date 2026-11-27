// SPDX-License-Identifier: GPL-3.0-or-later
#include "booksync/ui/widgets/Shelf.h"
namespace booksync::widgets {
WindowRequest Shelf::request(WindowSource source) { return {0, 3, LibraryBytes, source}; }
WidgetView Shelf::view(WidgetView viewValue) { return viewValue; }
bool Shelf::shelfCell(WidgetView viewValue, uint8_t style, WidgetBox itemBox, uint8_t itemIndex, WidgetCell& itemCell) {
  if (viewValue.viewState != WidgetState::Known || itemIndex >= viewValue.count || itemBox.line <= 0 ||
      itemBox.width <= 24 || itemBox.height <= 24)
    return false;
  const int top = itemBox.line + 22;
  if (!itemBox.textOnly && style == 0) {
    const int gap = 12;
    const int width = (itemBox.width - 24 - gap * (viewValue.count - 1)) / viewValue.count;
    int height = itemBox.height - top - itemBox.line - 20;
    if (height > 100) height = 100;
    if (height > 24 && width >= (itemBox.touch ? 50 : 25)) {
      itemCell = {12 + itemIndex * (width + gap), top, width, height + itemBox.line + 6, true, false};
      return true;
    }
  }
  int y = top;
  for (uint8_t i = 0; i <= itemIndex; ++i) {
    if (y + itemBox.line >= itemBox.height - 8) return false;
    int height = itemBox.line + 12;
    const bool progress = viewValue.rows[i].readingKnown && viewValue.rows[i].pages &&
                          (itemBox.textOnly || y + height + itemBox.line < itemBox.height - 8);
    if (progress && !itemBox.textOnly) height += itemBox.line + 8;
    if (itemBox.touch && height < 50) height = 50;
    const int remaining = itemBox.height - 8 - y;
    if (itemBox.touch && remaining < 50) return false;
    if (height > remaining) height = remaining;
    if (i == itemIndex) {
      itemCell = {12, y, itemBox.width - 24, height, false, progress};
      return true;
    }
    y += height;
  }
  return false;
}
}  // namespace booksync::widgets
