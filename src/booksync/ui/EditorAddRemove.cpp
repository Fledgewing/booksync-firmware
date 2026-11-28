// SPDX-License-Identifier: GPL-3.0-or-later
#include "booksync/ui/EditorAddRemove.h"
namespace booksync {
void EditorAddRemove::enter() {
  const auto page = library_.homePage();
  revision_ = page.revision;
  page_ = page.page;
  focus_ = 0;
  panel_ = page.textOnly || !page.revision ? Panel::Closed : Panel::Select;
}
uint8_t EditorAddRemove::rows() const {
  if (panel_ == Panel::Select) return library_.homePage().count + 3;  // Add, Reset, Done after tiles.
  if (panel_ == Panel::Add) return 3;
  if (panel_ == Panel::Selected) return 2;  // Remove, Done.
  if (panel_ == Panel::Reset) return 2;     // Keep first.
  return 1;
}
void EditorAddRemove::stepEditor(int direction) {
  if (panel_ == Panel::Closed || panel_ == Panel::Waiting) return;
  const int nextFocus = focus_ + direction;
  if (nextFocus >= 0 && nextFocus < rows()) focus_ = nextFocus;
}
void EditorAddRemove::chooseEditor(uint8_t index) {
  if (index >= rows() || panel_ == Panel::Waiting) return;
  focus_ = index;
  activateEditor();
}
void EditorAddRemove::back() {
  // Accepted work finishes atomically; Back retires only this presentation.
  if (panel_ == Panel::Select || panel_ == Panel::Waiting)
    panel_ = Panel::Closed;
  else if (panel_ == Panel::Error)
    enter();
  else if (panel_ != Panel::Closed) {
    panel_ = Panel::Select;
    focus_ = 0;
  }
}
void EditorAddRemove::send(LayoutEditKind kind, WidgetKind widget) {
  LayoutEdit request;
  request.kind = kind;
  request.widget = widget;
  request.revision = revision_;
  request.page = page_;
  request.index = selected_;
  if (library_.editLayout(request) == CommandStatus::Accepted)
    panel_ = Panel::Waiting;
  else {
    panel_ = Panel::Error;
    error_ = LayoutEditStatus::StorageError;
  }
  focus_ = 0;
}
void EditorAddRemove::activateEditor() {
  if (panel_ == Panel::Select) {
    const auto page = library_.homePage();
    if (page.revision != revision_ || page.page != page_) {
      panel_ = Panel::Error;
      error_ = LayoutEditStatus::Stale;
      return;
    }
    if (focus_ < page.count) {
      selected_ = focus_;
      panel_ = Panel::Selected;
      focus_ = 0;
    } else if (focus_ == page.count) {
      panel_ = Panel::Add;
      focus_ = 0;
    } else if (focus_ == page.count + 1) {
      panel_ = Panel::Reset;
      focus_ = 0;
    } else
      panel_ = Panel::Closed;
  } else if (panel_ == Panel::Selected) {
    if (focus_ == 0)
      send(LayoutEditKind::Remove);
    else
      back();
  } else if (panel_ == Panel::Add)
    send(LayoutEditKind::Add, static_cast<WidgetKind>(focus_));
  else if (panel_ == Panel::Reset) {
    if (focus_ == 1)
      send(LayoutEditKind::Reset);
    else
      back();
  } else if (panel_ == Panel::Error) {
    enter();
  }
}
bool EditorAddRemove::pollEditor() {
  if (panel_ != Panel::Waiting) return false;
  const auto editReply = library_.layoutEditResult();
  if (editReply.status == LayoutEditStatus::Pending || editReply.status == LayoutEditStatus::Idle) return false;
  if (editReply.status == LayoutEditStatus::Saved)
    enter();
  else {
    panel_ = Panel::Error;
    error_ = editReply.status;
    focus_ = 0;
  }
  return true;
}
}  // namespace booksync
