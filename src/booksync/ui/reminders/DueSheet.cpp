// SPDX-License-Identifier: GPL-3.0-or-later
#include "booksync/ui/reminders/DueSheet.h"
namespace booksync {
bool ReminderDueSheet::stageSheet(ReminderDue due) {
  if (view_.open) return false;
  view_.due = due;
  return true;
}

bool ReminderDueSheet::enterSheet(ReminderDue due, ReminderClaim sheetClaim, bool sleepAvailable) {
  if (view_.open || !sheetClaim.token || !sheetClaim.lifetime || due.source != sheetClaim.source ||
      due.kind != sheetClaim.kind || due.id != sheetClaim.id || due.revision != sheetClaim.revision ||
      (due.kind != ReminderKind::Time && due.kind != ReminderKind::Interval))
    return false;
  view_ = {};
  view_.due = due;
  view_.open = true;
  view_.sleepAvailable = sleepAvailable;
  claim_ = sheetClaim;
  draining_ = true;
  selected_ = sleep_ = false;
  touch_ = 255;
  ++revision_;
  return true;
}
bool ReminderDueSheet::enterSheet(const ReminderPresentation& sheetValue, ReminderClaim sheetClaim,
                                  bool sleepAvailable) {
  if (!sheetValue.ready || !enterSheet(sheetValue.due, sheetClaim, sleepAvailable)) return false;
  for (unsigned i = 0; i < 32; ++i) {
    view_.sourceLabel[i] = sheetValue.sourceLabel[i];
    view_.dueLabel[i] = sheetValue.dueLabel[i];
  }
  view_.sourceLabel[31] = view_.dueLabel[31] = 0;
  return true;
}
void ReminderDueSheet::sendSheetResponse(ReminderAction action) {
  if (!view_.open || view_.responsePending) return;
  if (action == ReminderAction::Sleep && !view_.sleepAvailable) {
    view_.error = ReminderSheetError::Sleep;
    view_.focus = 0;
    ++revision_;
    return;
  }
  if (action == ReminderAction::TurnOff && view_.due.source != ReminderSource::Local) return;
  pending_ = action;
  const auto responseState = library_.remindersRespond(claim_.token, claim_.lifetime, action);
  view_.responsePending = responseState.status == ReminderStatus::Pending;
  if (!view_.responsePending) {
    if (responseState.status == ReminderStatus::Saved) {
      sleep_ = responseState.sleep;
      view_.open = false;
    } else if (responseState.status == ReminderStatus::Stale) {
      retireSheet();
      return;
    } else {
      view_.error = action == ReminderAction::Sleep ? ReminderSheetError::Sleep : ReminderSheetError::Save;
      view_.focus = 0;
    }
  }
  ++revision_;
}
void ReminderDueSheet::activateSheetSelection() {
  if (view_.due.kind == ReminderKind::Interval)
    sendSheetResponse(view_.focus ? ReminderAction::TurnOff : ReminderAction::Done);
  else
    sendSheetResponse(view_.focus == 0   ? ReminderAction::GotIt
                      : view_.focus == 1 ? ReminderAction::Snooze10
                                         : ReminderAction::Sleep);
}
void ReminderDueSheet::sheetInput(ReminderSheetInput event, uint8_t action) {
  if (!view_.open || view_.responsePending) return;
  if (draining_) {
    if (event == ReminderSheetInput::ReleaseAll) draining_ = false;
    return;
  }
  switch (event) {
    case ReminderSheetInput::ReleaseAll:
      break;
    case ReminderSheetInput::SelectDown:
      selected_ = true;
      break;
    case ReminderSheetInput::SelectUp:
      if (selected_) {
        selected_ = false;
        activateSheetSelection();
      }
      break;
    case ReminderSheetInput::FocusPrevious:
      selected_ = false;
      touch_ = 255;
      if (view_.focus) {
        --view_.focus;
        ++revision_;
      }
      break;
    case ReminderSheetInput::FocusNext:
      selected_ = false;
      touch_ = 255;
      if (view_.focus + 1 < (view_.due.kind == ReminderKind::Time ? 3 : 2)) {
        ++view_.focus;
        ++revision_;
      }
      break;
    case ReminderSheetInput::PageTurn:
    case ReminderSheetInput::Back:
    case ReminderSheetInput::OutsideTap:
      sendSheetResponse(view_.due.kind == ReminderKind::Time ? ReminderAction::GotIt : ReminderAction::Done);
      break;
    case ReminderSheetInput::TouchDown:
      selected_ = false;
      touch_ = action;
      break;
    case ReminderSheetInput::TouchUp: {
      const bool sameAction = action == touch_;
      touch_ = 255;
      if (sameAction && action < (view_.due.kind == ReminderKind::Time ? 3 : 2)) {
        view_.focus = action;
        activateSheetSelection();
      }
      break;
    }
  }
}
bool ReminderDueSheet::pollSheet() {
  if (!view_.open || !view_.responsePending) return false;
  const auto responseState = library_.remindersResult();
  if (responseState.status == ReminderStatus::Pending) return false;
  view_.responsePending = false;
  if (responseState.status == ReminderStatus::Saved) {
    sleep_ = responseState.sleep;
    view_.open = false;
  } else if (responseState.status == ReminderStatus::Stale) {
    retireSheet();
    return true;
  } else {
    view_.error = pending_ == ReminderAction::Sleep ? ReminderSheetError::Sleep : ReminderSheetError::Save;
    view_.focus = 0;
  }
  ++revision_;
  return true;
}
void ReminderDueSheet::retireSheet() {
  if (claim_.lifetime) library_.remindersRetire(claim_.lifetime);
  view_.open = view_.responsePending = false;
  claim_ = {};
  selected_ = false;
  touch_ = 255;
  ++revision_;
}
}  // namespace booksync
