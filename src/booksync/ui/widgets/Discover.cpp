// SPDX-License-Identifier: GPL-3.0-or-later
#include "booksync/ui/widgets/Discover.h"

#include "booksync/ui/widgets/Shelf.h"
namespace booksync::widgets {
WindowRequest Discover::request() { return {0, 4, LibraryBytes, WindowSource::Random, WindowPresentation::Preview}; }
void Discover::paint(Canvas& canvas, WidgetView snapshot, WidgetBox box, uint8_t focusIndex) {
  Shelf shelf;
  shelf.paint(canvas, snapshot, WindowSource::Random, 0, box, focusIndex >= 2 ? focusIndex - 1 : 255);
  if (box.width < 180 || box.height < 180 || box.line <= 0 || box.line > 26) return;
  for (uint8_t i = 0; i < canvas.count; ++i) {
    auto& command = canvas.commands[i];
    if (command.kind == Drawing::Text && command.label == Label::ShelfTitle) {
      command.label = Label::DiscoverTitle;
      command.box.w = box.width - 220;
    }
    if (command.kind == Drawing::Text && command.label == Label::EmptyShelf) command.label = Label::EmptyDiscover;
  }
  const bool hasBooks = snapshot.viewState == WidgetState::Known && snapshot.count && snapshot.rows;
  if (hasBooks) {
    const auto another = anotherHeaderBox(box.width);
    if (focusIndex == 1) canvas.border(another, true, 2, 6);
    canvas.shuffle(another);
  }
}
}  // namespace booksync::widgets
