// SPDX-License-Identifier: GPL-3.0-or-later
#include "booksync/ui/EditorAddRemove.h"
namespace booksync {
void EditorAddRemove::enter() {
  const auto page = library_.homePage();
  revision_ = page.revision;
  page_ = page.page;
  focus_ = 0;
  panel_ = !page.revision ? Panel::Closed : Panel::Select;
}
uint8_t EditorAddRemove::rows() const {
  // X3 exposes only Add/Reset/Done; X4 also has six page actions.
  if (panel_ == Panel::Select) return library_.homePage().count + (library_.homePage().textOnly ? 3 : 9);
  if (panel_ == Panel::Name) return touchNaming_ ? 3 : 13;
  if (panel_ == Panel::Add) return library_.homePage().textOnly ? 3 : uint8_t(WidgetKind::Count);
  if (panel_ == Panel::Selected || panel_ == Panel::Options) return 2;  // Remove, Done.
  if (panel_ == Panel::Reset) return 2;                                 // Keep first.
  return 1;
}
uint8_t EditorAddRemove::firstVisibleRow(uint8_t capacity) const {
  if (panel_ != Panel::Add || !capacity || focus_ < capacity) return 0;
  return focus_ - capacity + 1;
}
void EditorAddRemove::stepEditor(int direction) {
  if (panel_ == Panel::Closed || panel_ == Panel::Waiting || panel_ == Panel::Paging) return;
  if (panel_ == Panel::Selected && !library_.homePage().textOnly) {
    geometryStep(direction > 0 ? LayoutStep::Next : LayoutStep::Previous);
    return;
  }
  const int nextFocus = focus_ + direction;
  if (nextFocus >= 0 && nextFocus < rows()) focus_ = nextFocus;
}
void EditorAddRemove::chooseEditor(uint8_t index) {
  if (index >= rows() || panel_ == Panel::Waiting || panel_ == Panel::Paging) return;
  focus_ = index;
  if (panel_ == Panel::Selected && !library_.homePage().textOnly) {
    if (index == 0)
      send(LayoutEditKind::Remove);
    else
      send(LayoutEditKind::Geometry);
  } else
    activateEditor();
}
void EditorAddRemove::back() {
  // Accepted work finishes atomically; Back retires only this presentation.
  if (panel_ == Panel::Select || panel_ == Panel::Waiting || panel_ == Panel::Paging)
    panel_ = Panel::Closed;
  else if (panel_ == Panel::Error)
    enter();
  else if (panel_ != Panel::Closed) {
    const bool carrying = panel_ == Panel::Selected || panel_ == Panel::Options;
    enter();
    const auto count = library_.homePage().count;
    if (carrying && panel_ == Panel::Select) focus_ = count ? (selected_ < count ? selected_ : count - 1) : 0;
    blocked_ = false;
  }
}
void EditorAddRemove::send(LayoutEditKind kind, WidgetKind widget) {
  LayoutEdit request;
  request.kind = kind;
  request.widget = widget;
  request.revision = revision_;
  request.page = page_;
  request.index = selected_;
  request.name = name_;
  pendingKind_ = kind;
  if (kind == LayoutEditKind::Geometry) {
    request.count = draft_.count;
    for (uint8_t i = 0; i < draft_.count; ++i) {
      const auto& tile = draft_.widgets[i];
      request.geometry[i] = {tile.x, tile.y, tile.w, tile.h};
    }
  }
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
      focus_ = 0;
      return;
    }
    if (focus_ < page.count) {
      selected_ = focus_;
      draft_ = page;
      blocked_ = false;
      panel_ = Panel::Selected;
      if (!page.textOnly) updateSizePreview();
      focus_ = 0;
    } else if (focus_ == page.count) {
      panel_ = Panel::Add;
      name_ = {};
      focus_ = 0;
    } else if (focus_ == page.count + 1) {
      panel_ = Panel::Reset;
      focus_ = 0;
    } else if (focus_ == page.count + 2)
      panel_ = Panel::Closed;
    else if (focus_ == page.count + 3 || focus_ == page.count + 4) {
      const int nextPageIndex = int(page.page) + (focus_ == page.count + 3 ? -1 : 1);
      if (nextPageIndex >= 0 && nextPageIndex < page.pages && library_.requestHomePage(nextPageIndex))
        panel_ = Panel::Paging;
    } else if (focus_ == page.count + 5) {
      if (page.pages < 16) send(LayoutEditKind::CreatePage);
    } else if (focus_ == page.count + 6) {
      name_ = page.title;
      panel_ = Panel::Name;
      focus_ = 0;
    } else {
      const int nextPageIndex = int(page.page) + (focus_ == page.count + 7 ? -1 : 1);
      if (nextPageIndex >= 0 && nextPageIndex < page.pages) {
        selected_ = nextPageIndex;
        send(LayoutEditKind::MovePage);
      }
    }
  } else if (panel_ == Panel::Name) {
    if (focus_ == (touchNaming_ ? 1 : 11)) back();
    if (focus_ == (touchNaming_ ? 2 : 12) && hasPageName()) send(LayoutEditKind::NamePage);
  } else if (panel_ == Panel::Selected) {
    if (library_.homePage().textOnly) {
      if (focus_ == 1)
        send(LayoutEditKind::Remove);
      else
        back();
    } else
      send(LayoutEditKind::Geometry);
  } else if (panel_ == Panel::Options) {
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
void EditorAddRemove::namePage(const char* name) {
  if ((panel_ != Panel::Name && panel_ != Panel::Add) || !name) return;
  uint8_t length = 0;
  while (length < 81 && name[length]) ++length;
  if (length == 81) return;
  for (uint8_t i = 0; i <= length; ++i) name_[i] = name[i];
}
bool EditorAddRemove::pollEditor() {
  if (panel_ == Panel::Paging) {
    const auto page = library_.homePage();
    if (page.page == page_ && page.revision == revision_) return false;
    enter();
    return true;
  }
  if (panel_ != Panel::Waiting) {
    if (panel_ == Panel::Closed || panel_ == Panel::Error) return false;
    const auto page = library_.homePage();
    if (page.revision != revision_ || page.page != page_) {
      panel_ = Panel::Error;
      error_ = LayoutEditStatus::Stale;
      focus_ = 0;
      return true;
    }
    if (focus_ >= rows()) {
      focus_ = rows() - 1;
      return true;
    }
    return false;
  }
  const auto editReply = library_.layoutEditResult();
  if (editReply.status == LayoutEditStatus::Pending || editReply.status == LayoutEditStatus::Idle) return false;
  if (editReply.status == LayoutEditStatus::Saved) {
    enter();
    const auto count = library_.homePage().count;
    if (pendingKind_ == LayoutEditKind::Geometry || pendingKind_ == LayoutEditKind::Remove)
      focus_ = count ? (selected_ < count ? selected_ : count - 1) : 0;
  } else {
    panel_ = Panel::Error;
    error_ = editReply.status;
    focus_ = 0;
  }
  return true;
}
void EditorAddRemove::geometryStep(LayoutStep movement) {
  if (panel_ != Panel::Selected || library_.homePage().textOnly) return;
  const auto preview = library_.previewLayout(draft_, selected_, movement);
  if (preview.status == LayoutEditStatus::Saved) {
    draft_ = preview.page;
    blocked_ = false;
    updateSizePreview();
  } else if (preview.status == LayoutEditStatus::Stale) {
    panel_ = Panel::Error;
    error_ = preview.status;
    focus_ = 0;
  } else
    blocked_ = true;
}
void EditorAddRemove::updateSizePreview() {
  const auto sizePreview = library_.previewLayout(draft_, selected_, LayoutStep::Size);
  nextW_ = sizePreview.status == LayoutEditStatus::Saved ? sizePreview.page.widgets[selected_].w : 0;
  nextH_ = sizePreview.status == LayoutEditStatus::Saved ? sizePreview.page.widgets[selected_].h : 0;
}
void EditorAddRemove::moreEditor() {
  if (library_.homePage().textOnly) return;
  if (panel_ == Panel::Selected)
    geometryStep(LayoutStep::Size);
  else if (panel_ == Panel::Select && focus_ < library_.homePage().count) {
    selected_ = focus_;
    panel_ = Panel::Options;
    focus_ = 0;
  }
}
}  // namespace booksync
