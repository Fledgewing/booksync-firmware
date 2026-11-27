// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

#include "booksync/interface/Delivery.h"
namespace booksync {
enum class Presentation {
  BackgroundRender,
  Message,
  Update,
  Network,
  Sync,
  Restart,
  Navigation,
  UserOpened,
  EndOfBook,
  PositionConsent
};
struct PresentationState {
  bool reading = false;  // Includes panels opened from the reader.
  bool visible = false;  // Closed/hidden consumers can adopt values, never repaint.
  bool sleeping = false;
};
class ReadingGate {
 public:
  // Main-loop check immediately before presentation, never a cached permission.
  static bool mayShow(Presentation event, PresentationState state, bool liveConsent = false);
};
struct PositionContext {
  // Non-reused identity tokens, not hashes: reader owns exact document/path
  // lifetime and increments run at invocation, before asynchronous activation.
  uint64_t document = 0;
  uint64_t path = 0;
  uint64_t run = 0;
};
class PositionPresentation {
 public:
  PositionPresentation(DeliveryTicket owner, PositionContext context) : owner(owner), context(context) {}
  PositionPresentation(const PositionPresentation&) = delete;
  PositionPresentation& operator=(const PositionPresentation&) = delete;
  bool eligible(DeliveryTicket current, PositionContext currentContext);
  // Claims exactly one approved draw; hidden/sleep guards do not consume it.
  bool claim(DeliveryTicket current, PositionContext currentContext, PresentationState state);
  void retire() { live = false; }

 private:
  DeliveryTicket owner;
  PositionContext context;
  bool live = true;
};
}  // namespace booksync
