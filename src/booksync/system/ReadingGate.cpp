// SPDX-License-Identifier: GPL-3.0-or-later
#include "ReadingGate.h"

#include "booksync/interface/Library.h"
namespace booksync {
bool ReadingGate::mayShow(Presentation event, PresentationState state, bool liveConsent) {
  if (!state.visible || state.sleeping) return false;
  switch (event) {
    case Presentation::Reminder:
    case Presentation::BackgroundRender:
      return false;
    case Presentation::PositionConsent:
      return state.reading && liveConsent;
    case Presentation::UserOpened:
    case Presentation::EndOfBook:
      return true;
    case Presentation::Message:
    case Presentation::Update:
    case Presentation::Network:
    case Presentation::Sync:
    case Presentation::Restart:
    case Presentation::Navigation:
      return !state.reading;
  }
  return false;
}
ReminderPermit ReadingGate::reminderPermit(Library& library, ReminderClaim claim, PresentationState state) {
  ReminderPermit permit;
  if (!state.visible || state.sleeping || !claim.id || !claim.token || !claim.lifetime ||
      claim.source != ReminderSource::Local)
    return permit;
  permit.proof_ = library.remindersAuthorize(claim);
  permit.granted_ =
      permit.proof_.allowed && permit.proof_.lifetime == claim.lifetime && permit.proof_.kind == claim.kind;
  return permit;
}
bool ReadingGate::drawReminder(ReminderPermit& permit, ReminderDrawContext now) {
  if (!permit.granted_) return false;
  const auto& proof = permit.proof_;
  const bool allowed = now.state.visible && !now.state.sleeping && !now.otherModal && now.lifetime == proof.lifetime &&
                       (proof.kind == ReminderKind::Time ? now.clockKnown && now.elapsed <= proof.validUntil
                                                         : now.page && now.state.reading);
  // A hidden, retired, expired or invalid-clock grant can never be revived.
  permit.granted_ = false;
  return allowed;
}
bool PositionPresentation::eligible(DeliveryTicket current, PositionContext now) {
  live = live && owner.owner == current.owner && owner.epoch == current.epoch && context.document != 0 &&
         context.path != 0 && context.run != 0 && context.document == now.document && context.path == now.path &&
         context.run == now.run && context.automatic == now.automatic;
  return live;
}
bool PositionPresentation::claim(DeliveryTicket current, PositionContext now, PresentationState state) {
  if (!eligible(current, now) || !ReadingGate::mayShow(Presentation::PositionConsent, state, true)) return false;
  retire();
  return true;
}
}  // namespace booksync
