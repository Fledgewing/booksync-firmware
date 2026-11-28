// SPDX-License-Identifier: GPL-3.0-or-later
#include "booksync/ui/widgets/Lists.h"
namespace booksync::widgets {
bool ListsCard::listTarget(WidgetBox box, uint8_t targetIndex, WidgetCell& cell) const {
  if (box.textOnly || box.width < 180 || box.height < 100) return false;
  if (!targetIndex) {
    const auto all = allHeaderBox(box.width);
    cell = {all.x, all.y, all.w, all.h};
    return true;
  }
  const bool expanded = box.width >= 360 && box.height >= 210;
  if (targetIndex > (expanded ? 3 : 1)) return false;
  cell = {12, 51 + (targetIndex - 1) * 50, box.width - 24, 50};
  return cell.y + cell.height <= box.height - 6;
}
uint8_t ListsCard::hitLists(WidgetBox box, int x, int y) const {
  if (x < 0 || y < 0 || x >= box.width || y >= box.height) return 255;
  WidgetCell cell;
  for (uint8_t i = 0; i <= 3; ++i)
    if (listTarget(box, i, cell) && x >= cell.x && x < cell.x + cell.width && y >= cell.y && y < cell.y + cell.height)
      return i;
  return 255;
}
void ListsCard::paint(Canvas& canvas, const ListsWidgetValues& listValue, WidgetBox box, uint8_t selected,
                      WidgetState checked) const {
  CollectionWidgetState paintState = listValue.state;
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
  canvas.text({12, 10, box.width - 156, 20}, Label::ListsTitle, true, true);
  const bool hasLists = paintState == CollectionWidgetState::Ready || paintState == CollectionWidgetState::Empty;
  if (hasLists) {
    canvas.text({box.width - 86, 18, 79, 20}, Label::AllLists, true, true, listValue.total, listValue.countKnown);
  }
  if (!hasLists || paintState == CollectionWidgetState::Empty || !listValue.count) {
    Label message = paintState == CollectionWidgetState::Empty ? Label::EmptyLists : Label::NotSaved;
    if (paintState == CollectionWidgetState::Off) message = Label::Off;
    if (paintState == CollectionWidgetState::Unsupported) message = Label::Unsupported;
    if (paintState == CollectionWidgetState::Error) message = Label::Error;
    if (paintState == CollectionWidgetState::Oversize) message = Label::OversizedData;
    const bool failure = paintState == CollectionWidgetState::Error || paintState == CollectionWidgetState::Oversize;
    canvas.text({12, failure ? 34 : 54, box.width - 24, failure ? 42 : box.line * 2}, message, true);
    if (failure) canvas.text({box.width - 76, box.height - 28, 64, 20}, Label::Retry, true, true);
    return;
  }
  const bool expanded = box.width >= 360 && box.height >= 210;
  const uint8_t cap = expanded ? 3 : 1;
  for (uint8_t i = 0; i < listValue.count && i < cap; ++i) {
    const auto& entry = listValue.entries[i];
    WidgetCell cell;
    if (!listTarget(box, i + 1, cell)) continue;
    if (selected == i + 1) canvas.border({cell.x - 4, cell.y, cell.width + 8, cell.height}, true, 2, 4);
    if (expanded) {
      canvas.title({20, cell.y + 15, box.width - 130, 20}, entry.name.data(), true, true, 179);
      canvas.text({box.width - 106, cell.y + 15, 94, 20}, Label::ListCountLink, true, false, entry.books,
                  entry.booksKnown);
      canvas.fill({12, cell.y + 49, box.width - 24, 1}, true);
    } else {
      canvas.title({14, 68, box.width - 88, 18}, entry.name.data(), true, true, 166);
      canvas.text({box.width - 65, 56, 54, 24}, Label::ListBooksNumber, true, true, entry.books, entry.booksKnown);
      canvas.text({box.width - 66, 81, 54, 17}, Label::ListBooksCaption, true, false, entry.books, entry.booksKnown);
    }
  }
  if (listValue.stale)
    canvas.microText({14, 32, box.width - 114, 20}, listValue.ageKnown ? Label::SavedAge : Label::SavedUnknown, true,
                     listValue.ageDays);
}
}  // namespace booksync::widgets
