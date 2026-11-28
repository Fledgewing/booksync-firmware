// SPDX-License-Identifier: GPL-3.0-or-later
#include "booksync/ui/widgets/Library.h"
namespace booksync::widgets {
bool LibraryCard::libraryTarget(WidgetBox box, uint8_t targetIndex, WidgetCell& cell) const {
  if (box.textOnly || box.width < 180 || box.height < 100) return false;
  if (!targetIndex) {
    cell = {0, 0, box.width, box.height};
    return true;
  }
  if (box.width < 360 || box.height < 210 || targetIndex > 3) return false;
  const int width = (box.width - 48) / 3;
  cell = {12 + (targetIndex - 1) * (width + 12), box.height - 64, width, 50};
  return true;
}
uint8_t LibraryCard::hitLibrary(WidgetBox box, int x, int y) const {
  if (x < 0 || y < 0 || x >= box.width || y >= box.height) return 255;
  WidgetCell cell;
  for (uint8_t i = 1; i <= 3; ++i)
    if (libraryTarget(box, i, cell) && x >= cell.x && x < cell.x + cell.width && y >= cell.y &&
        y < cell.y + cell.height)
      return i;
  return libraryTarget(box, 0, cell) ? 0 : 255;
}
void LibraryCard::paint(Canvas& canvas, const LibraryWidgetValues& summaryValue, WidgetBox box, uint8_t selected,
                        WidgetState checked) const {
  CollectionWidgetState paintState = summaryValue.state;
  if (checked != WidgetState::Known && checked != WidgetState::Empty) {
    paintState = checked == WidgetState::Error         ? CollectionWidgetState::Error
                 : checked == WidgetState::Oversize    ? CollectionWidgetState::Oversize
                 : checked == WidgetState::Off         ? CollectionWidgetState::Off
                 : checked == WidgetState::Unsupported ? CollectionWidgetState::Unsupported
                                                       : CollectionWidgetState::NotSaved;
  }
  canvas.clear();
  if (box.textOnly || box.width < 180 || box.height < 100 || box.line <= 0) return;
  canvas.border({0, 0, box.width, box.height}, true, 1, 9);

  canvas.text({12, 10, summaryValue.stale ? box.width - 152 : box.width - 24, 20}, Label::LibraryTitle, true, true);
  const bool hasCounts = paintState == CollectionWidgetState::Ready || paintState == CollectionWidgetState::Empty;
  if (!hasCounts) {
    Label message = Label::NotSaved;
    if (paintState == CollectionWidgetState::Off) message = Label::Off;
    if (paintState == CollectionWidgetState::Unsupported) message = Label::Unsupported;
    if (paintState == CollectionWidgetState::Error) message = Label::Error;
    if (paintState == CollectionWidgetState::Oversize) message = Label::OversizedData;
    const bool failure = paintState == CollectionWidgetState::Error || paintState == CollectionWidgetState::Oversize;
    canvas.text({12, 36, box.width - 24, 40}, message, true);
    if (failure) canvas.text({box.width - 76, box.height - 28, 64, 20}, Label::Retry, true, true);
    return;
  }
  const bool expanded = box.width >= 360 && box.height >= 210;
  const int half = (box.width - 32) / 2;
  const int numberY = expanded ? 42 : 31;
  const int numberHeight = expanded ? 44 : 32;
  canvas.text({12, numberY, half, numberHeight}, Label::LibraryBooksNumber, true, true, summaryValue.books,
              summaryValue.booksKnown);
  canvas.text({32 + half, numberY, half - 12, numberHeight}, Label::OnCardNumber, true, true, summaryValue.onCard,
              summaryValue.onCardKnown);
  canvas.commands[canvas.count - 1].scale = expanded ? 384 : 256;
  canvas.commands[canvas.count - 2].scale = expanded ? 384 : 256;
  canvas.fill({box.width / 2 + 5, expanded ? 44 : 32, 1, expanded ? 64 : 44}, true);
  canvas.microText({12, expanded ? 90 : 63, half, 17}, Label::LibraryBooksCaption, true);
  canvas.microText({32 + half, expanded ? 90 : 63, half - 12, 17}, Label::OnCardCaption, true);
  const int storageY = expanded ? 113 : box.height - 26;
  const int barWidth = box.width - 75;
  canvas.border({12, storageY + (expanded ? 5 : 4), barWidth, 5}, true);
  if (summaryValue.storageKnown)
    canvas.fill({12, storageY + (expanded ? 5 : 4), barWidth * summaryValue.storageUsedPercent / 100, 5}, true);
  canvas.microText({box.width - 56, storageY, 50, 22}, Label::StorageFree, true, summaryValue.freeBytes,
                   summaryValue.storageKnown);
  if (summaryValue.stale)
    canvas.microText({box.width - 134, 11, 122, 20}, summaryValue.ageKnown ? Label::SavedAge : Label::SavedUnknown,
                     true, summaryValue.ageDays);
  else {
    canvas.books({box.width - 33, 9, 21, 21});
  }
  if (expanded) {
    constexpr Label links[] = {Label::AuthorsLink, Label::SeriesLink, Label::CollectionsLink};
    for (uint8_t i = 1; i <= 3; ++i) {
      WidgetCell cell;
      if (!libraryTarget(box, i, cell)) continue;
      canvas.border({cell.x, cell.y, cell.width, cell.height}, true, selected == i ? 2 : 1, 23);
      if (!box.touch) canvas.border({cell.x - 4, cell.y - 4, cell.width + 8, cell.height + 8}, true, 2, 27);
      canvas.text({cell.x + 9, cell.y + 17, cell.width - 18, 20}, links[i - 1], true, true);
    }
  }
}
}  // namespace booksync::widgets
