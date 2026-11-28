// SPDX-License-Identifier: GPL-3.0-or-later
#include "booksync/ui/catalog/SourceList.h"
namespace booksync {
bool SourceList::enter(WindowSource source, uint16_t sourceListRows) {
  request_ = {};
  request_.source = source;
  request_.limit = sourceListRows;
  cursorCount_ = 0;
  if (!sourceListRows || sourceListRows > 12 || source == WindowSource::Continue || source == WindowSource::Unknown) {
    accepted_ = copied_ = false;
    return false;
  }
  return requestSourcePage(0);
}
bool SourceList::requestSourcePage(uint32_t offset) {
  if (openPending_) library_.retireOpen(pendingOpen_);
  openPending_ = false;
  request_.offset = offset;
  copied_ = false;
  publication_ = library_.subscribe().sequence;
  accepted_ = library_.requestWindow(request_);
  return accepted_;
}
void SourceList::retireSourceList() {
  if (openPending_) library_.retireOpen(pendingOpen_);
  openPending_ = accepted_ = copied_ = false;
}
bool SourceList::pollSourceList() {
  if (!accepted_) return false;
  const auto sequence = library_.subscribe().sequence;
  if (sequence == publication_) return false;
  publication_ = sequence;
  library_.readWindow(data_);
  copied_ = true;
  if (openPending_ && (data_.provenance != pendingOpen_.provenance || sourceView().viewState != WidgetState::Known)) {
    library_.retireOpen(pendingOpen_);
    openPending_ = false;
  }
  return true;
}
WidgetView SourceList::sourceView() const {
  if (!accepted_) return {WidgetState::Error};
  if (!copied_) return {WidgetState::Loading};
  if (data_.windowCount != 1) return {WidgetState::Error};
  const auto view = boundedWidget(data_, data_.windows[0], request_);
  if (view.viewState == WidgetState::Empty && request_.offset < view.total) return {WidgetState::NotSaved};
  return view;
}
bool SourceList::hasSourceNext() const {
  const auto view = sourceView();
  if (view.viewState != WidgetState::Known || !view.count || request_.offset > view.total) return false;
  return view.count < view.total - request_.offset;
}
bool SourceList::nextSourcePage() {
  if (!hasSourceNext()) return false;
  const auto offset = request_.offset;
  const auto count = sourceView().count;
  if (!requestSourcePage(offset + count)) return false;
  if (cursorCount_ == 16) {
    for (uint8_t i = 1; i < 16; ++i) cursors_[i - 1] = cursors_[i];
    --cursorCount_;
  }
  cursors_[cursorCount_++] = offset;
  return true;
}
bool SourceList::previousSourcePage() {
  if (!cursorCount_) return false;
  if (!requestSourcePage(cursors_[cursorCount_ - 1])) return false;
  --cursorCount_;
  return true;
}
CommandStatus SourceList::openSourceRow(uint8_t itemIndex) {
  const auto view = sourceView();
  if (view.viewState != WidgetState::Known || itemIndex >= view.count) return CommandStatus::Unavailable;
  const auto status = library_.open(view.rows[itemIndex].key);
  if (status == CommandStatus::Accepted) {
    pendingOpen_ = view.rows[itemIndex].key;
    openPending_ = true;
  }
  return status;
}
}  // namespace booksync
