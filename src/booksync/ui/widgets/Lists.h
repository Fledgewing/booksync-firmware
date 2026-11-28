// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/CollectionWidgets.h"
#include "booksync/ui/WidgetHost.h"
#include "booksync/ui/widgets/Canvas.h"
namespace booksync::widgets {
class ListsCard {
 public:
  auto paint(Canvas&, const ListsWidgetValues&, WidgetBox, uint8_t selected,
             WidgetState checked = WidgetState::Known) const -> void;
  bool listTarget(WidgetBox, uint8_t targetIndex, WidgetCell&) const;
  uint8_t hitLists(WidgetBox, int x, int y) const;
};
}  // namespace booksync::widgets
