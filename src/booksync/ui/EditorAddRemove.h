// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/Library.h"
namespace booksync {
// Approved X4 spatial and X3 Settings-list add/remove/reset operations.
class EditorAddRemove {
 public:
  enum class Panel : uint8_t { Closed, Select, Selected, Options, Add, Reset, Name, Paging, Waiting, Error };
  explicit EditorAddRemove(Library& library, bool touchNaming = false) : library_(library), touchNaming_(touchNaming) {}
  auto enter() -> void;
  auto back() -> void;
  auto stepEditor(int direction) -> void;
  auto activateEditor() -> void;
  auto chooseEditor(uint8_t index) -> void;
  auto pollEditor() -> bool;
  auto geometryStep(LayoutStep movement) -> void;
  auto moreEditor() -> void;
  const HomePage* draftPage() const { return panel_ == Panel::Selected && !draft_.textOnly ? &draft_ : nullptr; }
  bool blocked() const { return blocked_; }
  uint8_t nextWidth() const { return nextW_; }
  uint8_t nextHeight() const { return nextH_; }
  Panel panel() const { return panel_; }
  uint8_t focus() const { return focus_; }
  uint8_t selected() const { return selected_; }
  uint8_t rows() const;
  uint8_t firstVisibleRow(uint8_t capacity) const;
  WidgetKind editorAddKind(uint8_t index) const {
    constexpr WidgetKind kinds[] = {WidgetKind::Continue, WidgetKind::Shelf,  WidgetKind::Library, WidgetKind::Lists, WidgetKind::Discover, WidgetKind::Recommended,
                                    WidgetKind::Week,     WidgetKind::Streak, WidgetKind::Year,    WidgetKind::Monthly,
                                    WidgetKind::Today, WidgetKind::Pages, WidgetKind::When, WidgetKind::Records, WidgetKind::Goal, WidgetKind::Forecast};
    if (library_.homePage().textOnly)
      return index == 0 ? WidgetKind::Continue : index == 1 ? WidgetKind::Shelf : WidgetKind::Browse;
    return index < uint8_t(WidgetKind::Count) - 1 ? kinds[index] : WidgetKind::Continue;
  }
  char* nameBuffer() { return name_.data(); }
  const char* pageName() const { return name_.data(); }
  bool hasPageName() const {
    for (char c : name_) {
      if (!c) break;
      if (c != ' ') return true;
    }
    return false;
  }
  auto namePage(const char* name) -> void;
  LayoutEditStatus editorFailure() const { return error_; }

 private:
  Library& library_;
  const bool touchNaming_;
  HomePage draft_{};
  std::array<char, 81> name_{};
  bool blocked_ = false;
  uint8_t nextW_ = 0, nextH_ = 0;
  LayoutEditKind pendingKind_ = LayoutEditKind::Add;
  Panel panel_ = Panel::Closed;
  LayoutEditStatus error_ = LayoutEditStatus::Idle;
  uint32_t revision_ = 0;
  uint8_t page_ = 0, focus_ = 0, selected_ = 0;
  auto updateSizePreview() -> void;
  auto send(LayoutEditKind kind, WidgetKind widget = WidgetKind::Continue) -> void;
};
static_assert(sizeof(EditorAddRemove) <= 384);
}  // namespace booksync
