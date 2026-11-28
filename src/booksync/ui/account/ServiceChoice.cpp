// SPDX-License-Identifier: GPL-3.0-or-later
#include "booksync/ui/account/ServiceChoice.h"
namespace booksync {
namespace {
bool same(DeliveryTicket a, DeliveryTicket b) { return a.owner == b.owner && a.epoch == b.epoch; }
}  // namespace
void ServiceChoice::enter() {
  view_ = library_.accountView();
  // Malformed/unavailable services cannot be selected or sent back to Library.
  view_.serviceLabel.back() = 0;
  selected_ = view_.available && view_.service && view_.serviceLabel[0] ? 0 : 1;
  focus_ = selected_;
  live_ = true;
}
void ServiceChoice::retireChoice() {
  live_ = false;
  view_ = {};
  focus_ = selected_ = 0;
}
bool ServiceChoice::pollChoice() {
  if (!live_) return false;  // Closed presentations do not even read Library.
  const auto accountValue = library_.accountView();
  if (!same(view_.ticket, accountValue.ticket)) {
    retireChoice();  // A -> B -> A is stale even if the owner is equal again.
    return true;
  }
  return false;
}
void ServiceChoice::stepChoice(int direction) {
  if (!live_) return;
  const int targetFocus = focus_ + direction;
  if (targetFocus >= 0 && targetFocus < 4) focus_ = targetFocus;
}
void ServiceChoice::chooseChoice(uint8_t choiceIndex) {
  if (!live_ || choiceIndex >= 4) return;
  focus_ = choiceIndex;
  activateChoice();
}
CommandStatus ServiceChoice::activateChoice() {
  if (!live_) return CommandStatus::Unavailable;
  if (focus_ == 3) {
    retireChoice();
    return CommandStatus::Accepted;
  }
  if (focus_ < 2) {
    if (focus_ == 0 && (!view_.available || !view_.service || !view_.serviceLabel[0]))
      return CommandStatus::Unavailable;
    selected_ = focus_;
    return CommandStatus::Accepted;
  }
  // Validate again at dispatch; a queued account change must not accept this form.
  if (pollChoice()) return CommandStatus::Unavailable;
  const auto status = library_.selectService({view_.ticket, selected_ ? uint8_t(0) : view_.service});
  if (status == CommandStatus::Accepted) retireChoice();
  return status;
}
}  // namespace booksync
