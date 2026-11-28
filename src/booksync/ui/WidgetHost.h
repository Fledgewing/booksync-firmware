// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/Library.h"
namespace booksync {
enum class WidgetState : uint8_t { Loading, Known, Empty, NotSaved, Unsupported, Error, Oversize, Off };
struct WidgetView {
  WidgetState viewState = WidgetState::Loading;
  const BookRow* rows = nullptr;
  uint32_t total = 0;
  uint16_t count = 0;
  bool stale = false;
};
struct WidgetBox {
  int width = 0, height = 0, line = 0;
  bool textOnly = false, touch = false;
};
struct WidgetCell {
  int x = 0, y = 0, width = 0, height = 0;
  bool cover = false, progress = false;
};
static_assert(sizeof(WidgetCell) <= 32);
// One activity-owned payload reused across publications; widgets borrow const
// spans. No query, I/O, allocation or intent dispatch from view()/paint.
class WidgetHost {
 public:
  explicit WidgetHost(Library& library) : library_(library) {}
  auto enter(uint8_t page = 0) -> void;
  auto pollWidgets() -> bool;  // Main loop under RenderLock, outside paint.
  WidgetView view(uint8_t widget) const;
  const StatsCard* statsCard(uint8_t widget) const;
  const StatsYear* statsYear(uint8_t widget) const;
  const StatsMonths* statsMonths(uint8_t widget) const;
  const StatsHours* statsHours(uint8_t widget) const;
  const StatsRecords* statsRecords(uint8_t widget) const;
  auto widgetCell(uint8_t widget, WidgetBox itemBox, uint8_t itemIndex, WidgetCell& itemCell) const -> bool;
  uint8_t widgetRows(uint8_t widget, WidgetBox itemBox) const;
  uint8_t hitWidgetRow(uint8_t widget, WidgetBox itemBox, int x, int y) const;
  auto retryWidget(uint8_t widget) -> bool;
  CommandStatus dispatchWidget(uint8_t widget, uint8_t itemIndex = 0);
  const HomePage& page() const { return page_; }

 private:
  Library& library_;
  HomePage page_{};
  LibraryWindow data_{};
  StatsWidgetBatch stats_{};
  DeliveryTicket statsAccount_{};
  WindowBatchRequest batch_{};
  uint8_t wanted_ = 0;
  bool pending_ = true, accepted_ = false;
};
static_assert(sizeof(WidgetHost) <= 9 * 1024);
WidgetView boundedWidget(const LibraryWindow&, const WindowSpan&, WindowRequest);
}  // namespace booksync
