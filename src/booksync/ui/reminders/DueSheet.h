// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/Library.h"
namespace booksync {
enum class ReminderSheetInput : uint8_t {
  ReleaseAll,
  SelectDown,
  SelectUp,
  FocusPrevious,
  FocusNext,
  PageTurn,
  Back,
  OutsideTap,
  TouchDown,
  TouchUp
};
enum class ReminderSheetError : uint8_t { None, Save, Sleep };
struct ReminderSheetView {
  ReminderDue due{};
  char sourceLabel[32]{}, dueLabel[32]{};
  uint8_t focus = 0;
  bool open = false, responsePending = false, sleepAvailable = false;
  ReminderSheetError error = ReminderSheetError::None;
};
// Input and immutable sheetValue only. The native presenter owns the typed
// draw permit; this model never schedules, reads storage, sleeps or paints.
class ReminderDueSheet {
 public:
  explicit ReminderDueSheet(Library& library) : library_(library) {}
  auto stageSheet(const ReminderDue&) -> bool;
  auto enterSheet(ReminderDue, ReminderClaim, bool sleepAvailable) -> bool;
  auto enterSheet(const ReminderPresentation&, ReminderClaim, bool sleepAvailable) -> bool;
  auto sheetInput(ReminderSheetInput, uint8_t action = 255) -> void;
  auto pollSheet() -> bool;
  auto retireSheet() -> void;
  const ReminderSheetView& view() const { return view_; }
  uint64_t revision() const { return revision_; }
  ReminderClaim sheetClaimValue() const { return claim_; }
  bool draining() const { return draining_; }
  bool sleepRequested() const { return sleep_; }

 private:
  Library& library_;
  ReminderSheetView view_{};
  ReminderClaim claim_{};
  uint64_t revision_ = 0;
  uint8_t touch_ = 255;
  bool draining_ = true, selected_ = false, sleep_ = false;
  ReminderAction pending_ = ReminderAction::GotIt;
  auto sendSheetResponse(ReminderAction) -> void;
  auto activateSheetSelection() -> void;
};
static_assert(sizeof(ReminderDueSheet) <= ReminderDueBytes);
}  // namespace booksync
