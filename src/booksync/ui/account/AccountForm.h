// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/Library.h"
namespace booksync {
// One foreground-owned bounded form. No worker/store references or allocations.
class AccountForm {
 public:
  explicit AccountForm(Library& library) : library_(library) {}
  ~AccountForm();
  auto enterForm() -> void;
  auto retireForm() -> void;
  auto pollForm() -> bool;
  auto chooseForm(uint8_t index) -> void;
  auto stepForm(int direction) -> void;
  auto activateForm() -> void;
  auto appendForm(char byte) -> bool;
  auto eraseForm() -> void;
  auto advanceFormField() -> void;
  bool live() const { return live_; }
  uint8_t focus() const { return focus_; }
  uint8_t field() const { return field_; }
  uint8_t actions() const;
  const AccountFormView& view() const { return view_; }
  const AccountEntry& entry() const { return entry_; }

 private:
  Library& library_;
  AccountFormView view_{};
  AccountEntry entry_{};
  uint8_t focus_ = 0, field_ = 0;
  bool live_ = false;
  auto dispatchFormAction(AccountAction requestedAction) -> void;
};
static_assert(sizeof(AccountForm) <= 2048);
}  // namespace booksync
