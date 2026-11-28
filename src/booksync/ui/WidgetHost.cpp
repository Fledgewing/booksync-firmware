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
  const auto accountTicket = library_.accountView().ticket;
  if (batch_.statsWidgets &&
      (accountTicket.owner != statsAccount_.owner || accountTicket.epoch != statsAccount_.epoch)) {
    pending_ = true;
    stats_.requested = 0;
  }
  bool changed = false;
  if (pending_ || layout.revision != page_.revision || layout.page != page_.page) {
    page_ = layout;
    statsAccount_ = accountTicket;
    batch_ = {};
    stats_.requested = 0;
    for (auto& statsValue : stats_.cards) statsValue.clear();
    for (uint8_t i = 0; i < page_.count; ++i) {
      const auto& p = page_.widgets[i];
      if (isStatsWidget(p.kind) && !page_.textOnly) {
        batch_.statsWidgets |= statsWidgetBit(statsWidgetKind(p.kind));
        batch_.windows[i] = {0, 1, LibraryBytes, WindowSource::Unknown};
        continue;
      }
      batch_.windows[i] = p.kind == WidgetKind::Continue ? widgets::Continue::request()
                          : p.kind == WidgetKind::Shelf  ? widgets::Shelf::request(p.source, page_.textOnly)
                                                         : widgets::Browse::request();
    }
    // Unsupported/summary placeholders reserve one descriptor row, but do not
    // count as book consumers when sharing the remaining C3 publication budget.
    uint8_t bookConsumers = 0, placeholderRows = 0;
    for (uint8_t i = 0; i < page_.count; ++i) {
      if (batch_.windows[i].source == WindowSource::Unknown) {
        batch_.windows[i].limit = 1;
        ++placeholderRows;
      } else
        ++bookConsumers;
    }
    const uint16_t share = bookConsumers ? (LibraryRows - placeholderRows) / bookConsumers : 0;
    for (uint8_t i = 0; i < page_.count; ++i)
      if (batch_.windows[i].source != WindowSource::Unknown && batch_.windows[i].limit > share)
        batch_.windows[i].limit = share;
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
    if (batch_.statsWidgets) {
      library_.readStatsWidgets(stats_);
      for (auto& statsValue : stats_.cards) {
        if ((statsValue.source == StatsCardSource::Account || statsValue.accountLinked) &&
            (statsValue.provenance.owner != statsAccount_.owner ||
             statsValue.provenance.accountEpoch != statsAccount_.epoch))
          statsValue.clear();
      }
    }
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
  if (span.availability == Availability::Off) return {WidgetState::Off};
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
  return {span.count ? WidgetState::Known : WidgetState::Empty,
          span.count ? &batchValue.rows[span.first] : nullptr,
          span.total,
          span.count,
          batchValue.stale || span.stale,
          span.ageDays,
          span.ageKnown,
          span.count ? &batchValue.fileAvailability[span.first] : nullptr};
}
WidgetView WidgetHost::view(uint8_t widget) const {
  if (widget >= page_.count) return {WidgetState::Error};
  if (!accepted_) return {WidgetState::Error};
  if (data_.windowCount > LibraryBatchRequests) return {WidgetState::Error};
  if (isStatsWidget(page_.widgets[widget].kind)) {
    const auto* statsValue = statsCard(widget);
    if (!statsValue) return {};
    const auto state = statsValue->bytes > sizeof(StatsCard)    ? StatsCardState::Oversize
                       : statsValue->bytes != sizeof(StatsCard) ? StatsCardState::Unavailable
                                                                : statsValue->state;
    const auto mapped = state == StatsCardState::Ready         ? WidgetState::Known
                        : state == StatsCardState::Empty       ? WidgetState::Empty
                        : state == StatsCardState::Off         ? WidgetState::Off
                        : state == StatsCardState::Unsupported ? WidgetState::Unsupported
                        : state == StatsCardState::Oversize    ? WidgetState::Oversize
                        : state == StatsCardState::Unavailable ? WidgetState::Error
                                                               : WidgetState::NotSaved;
    return {mapped, nullptr, 0, 0, statsValue->stale};
  }
  if (widget >= data_.windowCount) return {};
  auto viewValue = boundedWidget(data_, data_.windows[widget], batch_.windows[widget]);
  if (page_.widgets[widget].kind == WidgetKind::Continue && data_.resume.supplied && widget < data_.windowCount &&
      data_.windows[widget].bytes <
          sizeof(WindowSpan) + data_.windows[widget].count * sizeof(BookRow) + sizeof(ContinueValues))
    return {WidgetState::Error};
  if (page_.widgets[widget].kind == WidgetKind::Continue && data_.resume.supplied &&
      (viewValue.viewState == WidgetState::Known || viewValue.viewState == WidgetState::Empty)) {
    if ((data_.resume.progressKnown && data_.resume.progressPercent > 100) ||
        (data_.resume.finishDayKnown && data_.resume.finishWeekday > 6) ||
        (data_.resume.timeLeftKnown && data_.resume.timeLeftUnit > ContinueTimeUnit::Hours))
      return {WidgetState::Error};
    if (data_.resume.seriesLabel.back() || data_.resume.chapterLabel.back()) return {WidgetState::Error};
    switch (data_.resume.state) {
      case ContinueState::Ready:
        break;
      case ContinueState::Empty:
        viewValue = {WidgetState::Empty};
        break;
      case ContinueState::Unsupported:
        viewValue = {WidgetState::Unsupported};
        break;
      case ContinueState::Error:
        viewValue = {WidgetState::Error};
        break;
      case ContinueState::Oversize:
        viewValue = {WidgetState::Oversize};
        break;
      case ContinueState::Off:
        viewValue = {WidgetState::Off};
        break;
      default:
        viewValue = {WidgetState::NotSaved};
        break;
    }
  }
  const auto kind = page_.widgets[widget].kind;
  return kind == WidgetKind::Continue ? widgets::Continue::view(viewValue)
         : kind == WidgetKind::Shelf  ? widgets::Shelf::view(viewValue)
                                      : widgets::Browse::view(viewValue);
}
const StatsCard* WidgetHost::statsCard(uint8_t widget) const {
  if (widget >= page_.count || page_.textOnly || !isStatsWidget(page_.widgets[widget].kind)) return nullptr;
  const auto id = statsWidgetKind(page_.widgets[widget].kind);
  if (!(stats_.requested & statsWidgetBit(id))) return nullptr;
  return &stats_.cards[unsigned(id)];
}
bool WidgetHost::widgetCell(uint8_t widget, WidgetBox itemBox, uint8_t itemIndex, WidgetCell& itemCell) const {
  if (widget >= page_.count || page_.widgets[widget].kind != WidgetKind::Shelf) return false;
  itemBox.textOnly = page_.textOnly;
  return widgets::Shelf::shelfCell(view(widget), page_.widgets[widget].style, itemBox, itemIndex, itemCell);
}
uint8_t WidgetHost::widgetRows(uint8_t widget, WidgetBox itemBox) const {
  if (widget >= page_.count) return 0;
  const auto viewValue = view(widget);
  if (page_.widgets[widget].kind != WidgetKind::Shelf) return 1;
  if (!page_.textOnly && viewValue.viewState == WidgetState::Empty) return 1;
  if (viewValue.viewState != WidgetState::Known) return 1;
  WidgetCell itemCell;
  uint8_t rows = 0;
  while (rows < viewValue.count && widgetCell(widget, itemBox, rows, itemCell)) ++rows;
  return rows + (page_.textOnly ? 0 : 1);
}
uint8_t WidgetHost::hitWidgetRow(uint8_t widget, WidgetBox itemBox, int x, int y) const {
  if (widget >= page_.count) return 255;
  if (page_.widgets[widget].kind != WidgetKind::Shelf) return 0;
  const auto viewValue = view(widget);
  const auto all = widgets::allHeaderBox(itemBox.width);
  if (!page_.textOnly && (viewValue.viewState == WidgetState::Known || viewValue.viewState == WidgetState::Empty) &&
      x >= all.x && x < all.x + all.w && y >= all.y && y < all.y + all.h)
    return 0;
  WidgetCell itemCell;
  for (uint8_t itemIndex = 0; itemIndex < viewValue.count && widgetCell(widget, itemBox, itemIndex, itemCell);
       ++itemIndex)
    if (x >= itemCell.x && x < itemCell.x + itemCell.width && y >= itemCell.y && y < itemCell.y + itemCell.height)
      return itemIndex + (page_.textOnly ? 0 : 1);
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
  const auto kind = page_.widgets[widget].kind;
  const bool saved = viewValue.viewState == WidgetState::Known || viewValue.viewState == WidgetState::Empty;
  if (kind == WidgetKind::Browse && saved)
    return page_.textOnly ? library_.submit(Intent::BrowseOnDevice) : library_.browseSource(WindowSource::OnDevice);
  if (kind == WidgetKind::Shelf && !page_.textOnly && saved) {
    if (!itemIndex) return library_.browseSource(page_.widgets[widget].source);
    --itemIndex;
    if (page_.widgets[widget].style == 1 && itemIndex >= 3) return CommandStatus::Unavailable;
  }
  if (viewValue.viewState != WidgetState::Known || itemIndex >= viewValue.count) return CommandStatus::Unavailable;
  if (page_.widgets[widget].kind == WidgetKind::Continue) return library_.submit(Intent::OpenLastBook);
  if (page_.widgets[widget].kind == WidgetKind::Shelf) return library_.open(viewValue.rows[itemIndex].key);
  return library_.submit(Intent::BrowseLibrary);
}
}  // namespace booksync

namespace booksync {
const StatsYear* WidgetHost::statsYear(uint8_t widget) const {
  return widget < page_.count && page_.widgets[widget].kind == WidgetKind::Year && statsCard(widget) ? &stats_.year
                                                                                                     : nullptr;
}
const StatsMonths* WidgetHost::statsMonths(uint8_t widget) const {
  return widget < page_.count && page_.widgets[widget].kind == WidgetKind::Monthly && statsCard(widget) ? &stats_.months
                                                                                                        : nullptr;
}
}  // namespace booksync

namespace booksync {
const StatsHours* WidgetHost::statsHours(uint8_t widget) const {
  return widget < page_.count && page_.widgets[widget].kind == WidgetKind::When && statsCard(widget) ? &stats_.hours
                                                                                                     : nullptr;
}
const StatsRecords* WidgetHost::statsRecords(uint8_t widget) const {
  return widget < page_.count && page_.widgets[widget].kind == WidgetKind::Records && statsCard(widget)
             ? &stats_.records
             : nullptr;
}
}  // namespace booksync

namespace booksync {
const StatsGoal* WidgetHost::statsGoal(uint8_t widget) const {
  return widget < page_.count && page_.widgets[widget].kind == WidgetKind::Goal && statsCard(widget) ? &stats_.goal
                                                                                                     : nullptr;
}
const StatsForecast* WidgetHost::statsForecast(uint8_t widget) const {
  return widget < page_.count && page_.widgets[widget].kind == WidgetKind::Forecast && statsCard(widget)
             ? &stats_.forecast
             : nullptr;
}
}  // namespace booksync
