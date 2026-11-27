// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/Library.h"
namespace booksync {
// Approved X4 editor operations. X3 Settings-list presentation awaits approval.
class EditorAddRemove {
 public:
  enum class Panel : uint8_t { Closed, Select, Selected, Add, Reset, Waiting, Error };
  explicit EditorAddRemove(Library& library) : library_(library) {}
  auto enter() -> void;
  auto back() -> void;
  auto stepEditor(int direction) -> void;
  auto activateEditor() -> void;
  auto chooseEditor(uint8_t index) -> void;
  auto pollEditor() -> bool;
  Panel panel() const { return panel_; }
  uint8_t focus() const { return focus_; }
  uint8_t selected() const { return selected_; }
  uint8_t rows() const;
  LayoutEditStatus editorFailure() const { return error_; }

 private:
  Library& library_;
  Panel panel_ = Panel::Closed;
  LayoutEditStatus error_ = LayoutEditStatus::Idle;
  uint32_t revision_ = 0;
  uint8_t page_ = 0, focus_ = 0, selected_ = 0;
  auto send(LayoutEditKind kind, WidgetKind widget = WidgetKind::Continue) -> void;
};
}  // namespace booksync
