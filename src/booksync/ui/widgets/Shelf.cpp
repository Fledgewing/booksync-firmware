// SPDX-License-Identifier: GPL-3.0-or-later
#include "booksync/ui/widgets/Shelf.h"
namespace booksync::widgets {
WindowRequest Shelf::request(WindowSource source, bool textOnly) {
  WindowRequest request{0, 3, LibraryBytes, source};
  if (!textOnly) {
    request.limit = source == WindowSource::AlsoReading || source == WindowSource::OnDevice ? 3 : 4;
    request.presentation = WindowPresentation::Preview;
  }
  return request;
}
WidgetView Shelf::view(WidgetView viewValue) { return viewValue; }
void Shelf::paint(Canvas& canvas, WidgetView viewValue, WindowSource source, uint8_t style, WidgetBox box,
                  uint8_t focusIndex) {
  if (box.width < 180 || box.height < 180 || box.line <= 0 || box.line > 26) return;
  canvas.border({0, 0, box.width, box.height}, true, 1, 9);
  canvas.text({12, 10, box.width - 156, 20}, Label::ShelfTitle, true, true, static_cast<uint64_t>(source),
              viewValue.year);
  if (source == WindowSource::Unopened)
    canvas.text({12, 32, box.width - 110, 18},
                viewValue.stale ? (viewValue.ageKnown ? Label::UnopenedSavedAge : Label::UnopenedSavedData)
                                : Label::UnopenedMeaning,
                true, false, viewValue.ageDays);
  const bool hasRows = viewValue.viewState == WidgetState::Known && viewValue.rows && viewValue.count;
  const bool completeEmpty = viewValue.viewState == WidgetState::Empty;
  if (hasRows || completeEmpty) {
    const auto all = allHeaderBox(box.width);
    canvas.text({all.x, 18, all.w, 20}, Label::AllBooks, true, true, viewValue.total);
  }
  if (!hasRows) {
    Label label = completeEmpty ? Label::EmptyShelf : Label::NotSaved;
    if (viewValue.viewState == WidgetState::Off) label = Label::Off;
    if (viewValue.viewState == WidgetState::Unsupported) label = Label::Unsupported;
    const bool failure = viewValue.viewState == WidgetState::Error || viewValue.viewState == WidgetState::Oversize;
    if (failure) label = viewValue.viewState == WidgetState::Oversize ? Label::OversizedData : Label::Error;
    canvas.text({12, 64, box.width - 24, box.line * 2}, label, true);
    if (failure) canvas.text({12, box.height - 58, box.width - 24, 50}, Label::Retry, true, true);
    return;
  }
  if (viewValue.stale && source != WindowSource::Unopened)
    canvas.text({12, 32, box.width - 110, 18}, viewValue.ageKnown ? Label::SavedAge : Label::SavedUnknown, true, false,
                viewValue.ageDays);
  const bool rows = style == 1 || box.width < 360;
  const uint16_t limit = rows ? 3 : 4;
  for (uint16_t i = 0; i < viewValue.count && i < limit; ++i) {
    const auto& book = viewValue.rows[i];
    if (rows) {
      const int y = 51 + i * 54;
      if (y + 54 > box.height - 8) break;
      if (focusIndex == i + 1) canvas.border({8, y, box.width - 16, 54}, true, 2, 4);
      canvas.cover({12, y + 5, 29, 44}, book, true);
      canvas.title({50, y + 5, box.width - 62, 20}, book.title.data(), true, true, 192);
      if (book.readingKnown && book.pages) {
        const int width = box.width - 108;
        const uint32_t page = book.page < book.pages ? book.page : book.pages;
        canvas.border({50, y + 34, width, 5}, true);
        canvas.fill({50, y + 34, int(uint64_t(width) * page / book.pages), 5}, true);
        canvas.text({box.width - 58, y + 27, 46, 22}, Label::BookProgress, true, false, book.page, book.pages);
      }
      if (i + 1 < viewValue.count && i + 1 < limit) canvas.fill({12, y + 53, box.width - 24, 1}, true);
    } else {
      const int width = (box.width - 48) / 4;
      const int x = 12 + i * (width + 8);
      const int height = box.height - 80;
      if (focusIndex == i + 1) canvas.border({x - 3, 48, width + 6, box.height - 52}, true, 2, 4);
      canvas.cover({x, 51, width, height}, book, true);
      if (viewValue.files && viewValue.files[i] == FileAvailability::OnCard)
        canvas.onCard({x + width - 20, 56, 14, 14});
      canvas.title({x, box.height - 26, width, 19}, book.title.data(), true, false, 176);
    }
  }
}
bool Shelf::shelfCell(WidgetView viewValue, uint8_t style, WidgetBox itemBox, uint8_t itemIndex, WidgetCell& itemCell) {
  if (viewValue.viewState != WidgetState::Known || itemIndex >= viewValue.count || itemBox.line <= 0 ||
      itemBox.width <= 24 || itemBox.height <= 24)
    return false;
  const int top = itemBox.line + 22;
  if (!itemBox.textOnly) {
    if (style == 1 || itemBox.width < 360) {
      const int y = 51 + itemIndex * 54;
      if (itemIndex >= 3 || y + 54 > itemBox.height - 8) return false;
      itemCell = {12, y,    itemBox.width - 24,
                  54, true, viewValue.rows[itemIndex].readingKnown && viewValue.rows[itemIndex].pages};
    } else {
      if (itemIndex >= 4) return false;
      const int width = (itemBox.width - 48) / 4;
      if (width < (itemBox.touch ? 50 : 25)) return false;
      itemCell = {12 + itemIndex * (width + 8), 51, width, itemBox.height - 58, true, false};
    }
    return true;
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
