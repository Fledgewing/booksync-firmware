// SPDX-License-Identifier: GPL-3.0-or-later
#include "booksync/ui/account/AccountForm.h"
namespace booksync {
namespace {
bool formTicketsMatch(DeliveryTicket a, DeliveryTicket b) { return a.owner == b.owner && a.epoch == b.epoch; }
void wipe(AccountEntry& entry) {
  for (auto& c : entry.server) {
    volatile char* p = &c;
    *p = 0;
  }
  for (auto& c : entry.user) {
    volatile char* p = &c;
    *p = 0;
  }
  for (auto& c : entry.password) {
    volatile char* p = &c;
    *p = 0;
  }
}
}  // namespace
AccountForm::~AccountForm() { wipe(entry_); }
void AccountForm::enterForm() {
  wipe(entry_);
  view_ = {};
  view_.origin = library_.accountView().ticket;
  if (library_.accountAction({view_.origin, 0, AccountAction::Begin}) != CommandStatus::Accepted) return;
  library_.readAccountForm(view_);
  view_.link.back() = 0;
  live_ = view_.form != 0;
  focus_ = field_ = 0;
}
void AccountForm::retireForm() {
  if (live_) library_.accountAction({view_.account.ticket, view_.form, AccountAction::Cancel});
  live_ = false;
  wipe(entry_);
  view_ = {};
  focus_ = field_ = 0;
}
bool AccountForm::pollForm() {
  if (!live_) return false;
  // Member-sized result storage avoids putting the 368-byte view on a C3 stack.
  const auto currentFormAccount = library_.accountView();
  // Read into the cached member only after saving the bounded correlation scalars.
  const auto expected = view_.account.ticket;
  const auto form = view_.form, revision = view_.revision;
  library_.readAccountForm(view_);
  const bool correlated = view_.form == form && formTicketsMatch(view_.origin, expected);
  const bool acceptedSuccess = correlated && view_.scene == AccountScene::In &&
                               formTicketsMatch(currentFormAccount.ticket, view_.account.ticket);
  if (view_.form != form || view_.revision < revision ||
      !formTicketsMatch(currentFormAccount.ticket, view_.account.ticket) ||
      (!formTicketsMatch(currentFormAccount.ticket, expected) && !acceptedSuccess)) {
    // Never send Cancel with a new owner's ticket.
    live_ = false;
    wipe(entry_);
    view_ = {};
    return true;
  }
  view_.link.back() = 0;
  if (revision == view_.revision) return false;
  focus_ = 0;
  if (view_.scene != AccountScene::Manual) wipe(entry_);
  return true;
}
uint8_t AccountForm::actions() const {
  switch (view_.scene) {
    case AccountScene::Limited:
    case AccountScene::Old:
    case AccountScene::Busy:
      return 1;
    case AccountScene::Phone:
      return 3;
    default:
      return 2;
  }
}
void AccountForm::stepForm(int direction) {
  if (!live_) return;
  const int nextFormFocus = focus_ + direction;
  if (nextFormFocus >= 0 && nextFormFocus < actions()) focus_ = nextFormFocus;
}
void AccountForm::chooseForm(uint8_t index) {
  if (!live_ || index >= actions()) return;
  focus_ = index;
  activateForm();
}
void AccountForm::dispatchFormAction(AccountAction requestedAction) {
  if (library_.accountAction({view_.account.ticket, view_.form, requestedAction}) == CommandStatus::Accepted)
    pollForm();
}
void AccountForm::activateForm() {
  if (!live_) return;
  pollForm();
  if (!live_) return;
  switch (view_.scene) {
    case AccountScene::Out:
    case AccountScene::In:
      if (focus_ == 0)
        dispatchFormAction(AccountAction::Phone);
      else
        retireForm();
      break;
    case AccountScene::Manual:
      if (focus_ == 0)
        retireForm();
      else {
        library_.submitAccount({view_.account.ticket, view_.form, AccountAction::Manual}, entry_);
        wipe(entry_);
        pollForm();
      }
      break;
    case AccountScene::Phone:
      if (focus_ == 0)
        dispatchFormAction(AccountAction::Check);
      else if (focus_ == 1) {
        wipe(entry_);
        dispatchFormAction(AccountAction::Manual);
      } else
        retireForm();
      break;
    case AccountScene::Wait:
      if (focus_ == 0)
        dispatchFormAction(AccountAction::Check);
      else
        retireForm();
      break;
    case AccountScene::Expired:
    case AccountScene::Failed:
    case AccountScene::TwoStep:
      if (focus_ == 0)
        dispatchFormAction(AccountAction::Phone);
      else
        retireForm();
      break;
    default:
      retireForm();
      break;
  }
}
bool AccountForm::appendForm(char byte) {
  if (!live_ || view_.scene != AccountScene::Manual || byte < 32 || byte > 126) return false;
  char* p = field_ == 0 ? entry_.server.data() : field_ == 1 ? entry_.user.data() : entry_.password.data();
  const uint16_t capacity = field_ == 0   ? sizeof(entry_.server)
                            : field_ == 1 ? sizeof(entry_.user)
                                          : sizeof(entry_.password);
  uint16_t entryLength = 0;
  while (entryLength < capacity && p[entryLength]) ++entryLength;
  if (entryLength + 1 >= capacity) return false;
  p[entryLength] = byte;
  p[entryLength + 1] = 0;
  return true;
}
void AccountForm::eraseForm() {
  if (!live_ || view_.scene != AccountScene::Manual) return;
  char* p = field_ == 0 ? entry_.server.data() : field_ == 1 ? entry_.user.data() : entry_.password.data();
  uint16_t entryLength = 0;
  while (p[entryLength]) ++entryLength;
  if (entryLength) p[entryLength - 1] = 0;
}
void AccountForm::advanceFormField() {
  if (live_ && view_.scene == AccountScene::Manual) field_ = (field_ + 1) % 3;
}
}  // namespace booksync
