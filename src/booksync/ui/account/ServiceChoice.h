// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/Library.h"
namespace booksync {
// Approved B5 server-choice only. Account forms remain behind design approval.
// Owned by the foreground activity; pollChoice/input run outside paint under RenderLock.
class ServiceChoice {
 public:
  explicit ServiceChoice(Library& library) : library_(library) {}
  auto enter() -> void;
  auto retireChoice() -> void;
  auto pollChoice() -> bool;
  auto stepChoice(int direction) -> void;
  auto chooseChoice(uint8_t choiceIndex) -> void;
  CommandStatus activateChoice();
  bool live() const { return live_; }
  uint8_t focus() const { return focus_; }
  uint8_t selected() const { return selected_; }
  const AccountView& view() const { return view_; }

 private:
  Library& library_;
  AccountView view_{};
  uint8_t focus_ = 0, selected_ = 0;
  bool live_ = false;
};
static_assert(sizeof(ServiceChoice) <= 96);
}  // namespace booksync
