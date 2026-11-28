// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/CollectionWidgets.h"
#include "booksync/ui/WidgetHost.h"
#include "booksync/ui/widgets/Canvas.h"
namespace booksync::widgets {
class LibraryCard {
 public:
  auto paint(Canvas&, const LibraryWidgetValues&, WidgetBox, uint8_t selected,
             WidgetState checked = WidgetState::Known) const -> void;
  bool libraryTarget(WidgetBox, uint8_t targetIndex, WidgetCell&) const;
  uint8_t hitLibrary(WidgetBox, int x, int y) const;
};
}  // namespace booksync::widgets
