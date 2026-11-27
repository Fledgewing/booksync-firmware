// SPDX-License-Identifier: GPL-3.0-or-later
#include "booksync/ui/WidgetHost.h"

#include "booksync/ui/widgets/Browse.h"
#include "booksync/ui/widgets/Continue.h"
#include "booksync/ui/widgets/Shelf.h"
namespace booksync {
void WidgetHost::enter(uint8_t page) {
  wanted_ = page;
  pending_ = true;
  accepted_ = false;
  data_.publication = 0;
  data_.windowCount = 0;
  library_.requestHomePage(page);
}
bool WidgetHost::pollWidgets() {
  const auto layout = library_.homePage();
  if (!layout.revision || layout.page != wanted_ || layout.count > HomeWidgets || !layout.cols || !layout.rows ||
      layout.cols > 12 || layout.rows > 12 || layout.title.back())
    return false;
  bool changed = false;
  if (pending_ || layout.revision != page_.revision || layout.page != page_.page) {
    page_ = layout;
    batch_ = {};
    for (uint8_t i = 0; i < page_.count; ++i) {
      const auto& p = page_.widgets[i];
      batch_.windows[i] = p.kind == WidgetKind::Continue ? widgets::Continue::request()
                          : p.kind == WidgetKind::Shelf  ? widgets::Shelf::request(p.source)
                                                         : widgets::Browse::request();
      // Fair independent itemIndex shares; aggregate remains within the C3 window.
      const uint16_t share = LibraryRows / page_.count;
      if (batch_.windows[i].limit > share) batch_.windows[i].limit = share;
    }
    batch_.count = page_.count;
    data_.publication = library_.subscribe().sequence;
    accepted_ = !batch_.count || library_.requestBatch(batch_);
    data_.windowCount = 0;
    pending_ = false;
    changed = true;
  }
  // The public subscription is cheap and never reads storage. A single copied
  // batch serves every visible widget, including publications with sibling errors.
  const auto publication = library_.subscribe().sequence;
  if (accepted_ && publication != data_.publication) {
    library_.readWindow(data_);
    return true;
  }
  return changed;
}
WidgetView boundedWidget(const LibraryWindow& batchValue, const WindowSpan& span, WindowRequest request) {
  if (!(span.request == request)) return {};
  if (span.bytes > request.bytes || span.bytes > LibraryBytes) return {WidgetState::Oversize};
  if (span.count > request.limit || span.first > LibraryRows || span.count > LibraryRows - span.first ||
      span.first > batchValue.count || span.count > batchValue.count - span.first ||
      span.bytes < sizeof(WindowSpan) + span.count * sizeof(BookRow))
    return {WidgetState::Error};
  if (span.availability == Availability::Unavailable) return {WidgetState::Error};
  if (span.availability == Availability::Unsupported) return {WidgetState::Unsupported};
  if (span.availability == Availability::NotSaved) return {WidgetState::NotSaved};
  for (uint16_t i = 0; i < span.count; ++i) {
    bool terminated = false;
    for (char c : batchValue.rows[span.first + i].title)
      if (!c) {
        terminated = true;
        break;
      }
    if (!terminated) return {WidgetState::Error};
  }
  return {span.count ? WidgetState::Known : WidgetState::Empty, span.count ? &batchValue.rows[span.first] : nullptr,
          span.total, span.count, batchValue.stale};
}
WidgetView WidgetHost::view(uint8_t widget) const {
  if (widget >= page_.count) return {WidgetState::Error};
  if (!accepted_) return {WidgetState::Error};
  if (data_.windowCount > LibraryBatchRequests) return {WidgetState::Error};
  if (widget >= data_.windowCount) return {};
  const auto viewValue = boundedWidget(data_, data_.windows[widget], batch_.windows[widget]);
  const auto kind = page_.widgets[widget].kind;
  return kind == WidgetKind::Continue ? widgets::Continue::view(viewValue)
         : kind == WidgetKind::Shelf  ? widgets::Shelf::view(viewValue)
                                      : widgets::Browse::view(viewValue);
}
bool WidgetHost::widgetCell(uint8_t widget, WidgetBox itemBox, uint8_t itemIndex, WidgetCell& itemCell) const {
  if (widget >= page_.count || page_.widgets[widget].kind != WidgetKind::Shelf) return false;
  itemBox.textOnly = page_.textOnly;
  return widgets::Shelf::shelfCell(view(widget), page_.widgets[widget].style, itemBox, itemIndex, itemCell);
}
uint8_t WidgetHost::widgetRows(uint8_t widget, WidgetBox itemBox) const {
  if (widget >= page_.count) return 0;
  const auto viewValue = view(widget);
  if (page_.widgets[widget].kind != WidgetKind::Shelf || viewValue.viewState != WidgetState::Known) return 1;
  WidgetCell itemCell;
  uint8_t rows = 0;
  while (rows < viewValue.count && widgetCell(widget, itemBox, rows, itemCell)) ++rows;
  return rows;
}
uint8_t WidgetHost::hitWidgetRow(uint8_t widget, WidgetBox itemBox, int x, int y) const {
  if (widget >= page_.count) return 255;
  if (page_.widgets[widget].kind != WidgetKind::Shelf) return 0;
  const auto viewValue = view(widget);
  WidgetCell itemCell;
  for (uint8_t itemIndex = 0; itemIndex < viewValue.count && widgetCell(widget, itemBox, itemIndex, itemCell);
       ++itemIndex)
    if (x >= itemCell.x && x < itemCell.x + itemCell.width && y >= itemCell.y && y < itemCell.y + itemCell.height)
      return itemIndex;
  return 255;
}
bool WidgetHost::retryWidget(uint8_t widget) {
  if (pending_ || widget >= page_.count) return false;
  const auto widgetState = view(widget);
  if (widgetState.viewState != WidgetState::Error && widgetState.viewState != WidgetState::Oversize) return false;
  return library_.requestBatch(batch_);
}
CommandStatus WidgetHost::dispatchWidget(uint8_t widget, uint8_t itemIndex) {
  if (pending_ || widget >= page_.count) return CommandStatus::Unavailable;
  const auto viewValue = view(widget);
  if (viewValue.viewState != WidgetState::Known || itemIndex >= viewValue.count) return CommandStatus::Unavailable;
  if (page_.widgets[widget].kind == WidgetKind::Continue) return library_.submit(Intent::OpenLastBook);
  if (page_.widgets[widget].kind == WidgetKind::Shelf) return library_.open(viewValue.rows[itemIndex].key);
  return library_.submit(Intent::BrowseOnDevice);
}
}  // namespace booksync
