// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/ui/WidgetHost.h"
namespace booksync {
// Foreground controller for an existing saved-source destination. One bounded
// request/copy per page; no storage, source fallback or transport in UI.
class SourceList {
 public:
  explicit SourceList(Library& library) : library_(library) {}
  auto enter(WindowSource source, uint16_t sourceListRows) -> bool;
  auto retireSourceList() -> void;
  auto pollSourceList() -> bool;
  auto retrySourcePage() -> bool { return requestSourcePage(request_.offset); }
  auto nextSourcePage() -> bool;
  auto previousSourcePage() -> bool;
  auto openSourceRow(uint8_t itemIndex) -> CommandStatus;
  WidgetView sourceView() const;
  WindowSource sourceKind() const { return request_.source; }
  uint32_t sourceOffset() const { return request_.offset; }
  bool hasSourceNext() const;
  bool hasSourcePrevious() const { return cursorCount_ > 0; }

 private:
  Library& library_;
  LibraryWindow data_{};
  WindowRequest request_{};
  std::array<uint32_t, 16> cursors_{};
  BookKey pendingOpen_{};
  bool openPending_ = false;
  uint64_t publication_ = 0;
  uint8_t cursorCount_ = 0;
  bool accepted_ = false, copied_ = false;
  auto requestSourcePage(uint32_t offset) -> bool;
};
static_assert(sizeof(SourceList) <= 7 * 1024);
}  // namespace booksync
