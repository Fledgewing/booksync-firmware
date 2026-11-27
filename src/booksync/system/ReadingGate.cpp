// SPDX-License-Identifier: GPL-3.0-or-later
#include "ReadingGate.h"
namespace booksync {
bool ReadingGate::mayShow(Presentation event, PresentationState state, bool liveConsent) {
  if (!state.visible || state.sleeping) return false;
  switch (event) {
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
bool PositionPresentation::eligible(DeliveryTicket current, PositionContext now) {
  live = live && owner.owner == current.owner && owner.epoch == current.epoch && context.document != 0 &&
         context.path != 0 && context.run != 0 && context.document == now.document && context.path == now.path &&
         context.run == now.run;
  return live;
}
bool PositionPresentation::claim(DeliveryTicket current, PositionContext now, PresentationState state) {
  if (!eligible(current, now) || !ReadingGate::mayShow(Presentation::PositionConsent, state, true)) return false;
  retire();
  return true;
}
}  // namespace booksync
