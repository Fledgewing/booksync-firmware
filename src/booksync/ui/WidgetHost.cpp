// SPDX-License-Identifier: GPL-3.0-or-later
#include "booksync/ui/WidgetHost.h"

#include "booksync/ui/widgets/Browse.h"
#include "booksync/ui/widgets/Continue.h"
#include "booksync/ui/widgets/Shelf.h"
#include "booksync/ui/widgets/TimeDrawing.h"
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
    time_ = {};
    bool auxiliary = false;
    for (uint8_t i = 0; i < page_.count; ++i) {
      const auto& p = page_.widgets[i];
      if (p.kind != WidgetKind::Continue && p.kind != WidgetKind::Shelf && p.kind != WidgetKind::Browse)
        auxiliary = true;
      if (isTimeWidget(p.kind) && !page_.textOnly) {
        batch_.timeWidgets |= timeWidgetBit(p.kind == WidgetKind::Clock ? TimeWidget::Clock : TimeWidget::Moon);
        batch_.windows[i] = {0, 1, LibraryBytes, WindowSource::Unknown};
        continue;
      }
      batch_.windows[i] = p.kind == WidgetKind::Continue ? widgets::Continue::request()
                          : p.kind == WidgetKind::Shelf  ? widgets::Shelf::request(p.source)
                                                         : widgets::Browse::request();
    }
    uint16_t requestedRows = 0;
    for (uint8_t i = 0; i < page_.count; ++i) requestedRows += batch_.windows[i].limit;
    // New scalar cards must not reduce a four-cover preview when the complete
    // visible page already fits the global row cap. Keep the original fair
    // shares for old/text pages and genuinely oversubscribed custom grids.
    if (!auxiliary || page_.textOnly || requestedRows > LibraryRows) {
      const uint16_t share = page_.count ? LibraryRows / page_.count : 0;
      for (uint8_t i = 0; i < page_.count; ++i)
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
    if (batch_.timeWidgets) library_.readTimeWidgets(time_);
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
  if (isTimeWidget(page_.widgets[widget].kind)) {
    const auto* clock = clockCard(widget);
    const auto* moon = moonCard(widget);
    if (!clock && !moon) return {};
    const auto status = clock ? clock->status : moon->status;
    const auto state = clock ? widgets::boundedClockCard(*clock) : widgets::boundedMoonCard(*moon);
    const auto mapped = state == TimeCardState::Ready         ? WidgetState::Known
                        : state == TimeCardState::Empty       ? WidgetState::Empty
                        : state == TimeCardState::Off         ? WidgetState::Off
                        : state == TimeCardState::Unsupported ? WidgetState::Unsupported
                        : state == TimeCardState::Oversize    ? WidgetState::Oversize
                        : state == TimeCardState::Unavailable ? WidgetState::Error
                                                              : WidgetState::NotSaved;
    return {mapped, nullptr, 0, 0, status.stale};
  }
  if (widget >= data_.windowCount) return {};
  const auto viewValue = boundedWidget(data_, data_.windows[widget], batch_.windows[widget]);
  const auto kind = page_.widgets[widget].kind;
  return kind == WidgetKind::Continue ? widgets::Continue::view(viewValue)
         : kind == WidgetKind::Shelf  ? widgets::Shelf::view(viewValue)
                                      : widgets::Browse::view(viewValue);
}
const ClockWidget* WidgetHost::clockCard(uint8_t widget) const {
  if (widget >= page_.count || page_.textOnly || page_.widgets[widget].kind != WidgetKind::Clock ||
      !(time_.requested & timeWidgetBit(TimeWidget::Clock)))
    return nullptr;
  return &time_.clock;
}
const MoonWidget* WidgetHost::moonCard(uint8_t widget) const {
  if (widget >= page_.count || page_.textOnly || page_.widgets[widget].kind != WidgetKind::Moon ||
      !(time_.requested & timeWidgetBit(TimeWidget::Moon)))
    return nullptr;
  return &time_.moon;
}
bool WidgetHost::widgetCell(uint8_t widget, WidgetBox itemBox, uint8_t itemIndex, WidgetCell& itemCell) const {
  if (widget >= page_.count || page_.widgets[widget].kind != WidgetKind::Shelf) return false;
  itemBox.textOnly = page_.textOnly;
  return widgets::Shelf::shelfCell(view(widget), page_.widgets[widget].style, itemBox, itemIndex, itemCell);
}
uint8_t WidgetHost::widgetRows(uint8_t widget, WidgetBox itemBox) const {
  if (widget >= page_.count) return 0;
  if (isTimeWidget(page_.widgets[widget].kind)) return 0;
  const auto viewValue = view(widget);
  if (page_.widgets[widget].kind != WidgetKind::Shelf || viewValue.viewState != WidgetState::Known) return 1;
  WidgetCell itemCell;
  uint8_t rows = 0;
  while (rows < viewValue.count && widgetCell(widget, itemBox, rows, itemCell)) ++rows;
  return rows;
}
uint8_t WidgetHost::hitWidgetRow(uint8_t widget, WidgetBox itemBox, int x, int y) const {
  if (widget >= page_.count) return 255;
  if (isTimeWidget(page_.widgets[widget].kind)) {
    const auto state = view(widget).viewState;
    return (state == WidgetState::Error || state == WidgetState::Oversize) && x >= itemBox.width - 70 &&
                   x < itemBox.width - 10 && y >= itemBox.height - 50 && y < itemBox.height
               ? 0
               : 255;
  }
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
