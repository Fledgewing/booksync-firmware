// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

#include "booksync/interface/Delivery.h"
#include "booksync/interface/Reminders.h"
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
  PositionConsent,
  Reminder
};
struct PresentationState {
  bool reading = false;  // Includes panels opened from the reader.
  bool visible = false;  // Closed/hidden consumers can adopt values, never repaint.
  bool sleeping = false;
};
class Library;
struct ReminderDrawContext {
  PresentationState state{};
  uint64_t lifetime = 0, elapsed = 0;
  bool clockKnown = false, page = false, otherModal = false;
};
class ReminderPermit {
 public:
  ReminderPermit() = default;
  ReminderPermit(const ReminderPermit&) = delete;
  ReminderPermit& operator=(const ReminderPermit&) = delete;
  ReminderPermit(ReminderPermit&& other) noexcept : granted_(other.granted_), proof_(other.proof_) {
    other.granted_ = false;
  }
  ReminderPermit& operator=(ReminderPermit&& other) noexcept {
    granted_ = other.granted_;
    proof_ = other.proof_;
    other.granted_ = false;
    return *this;
  }

 private:
  friend class ReadingGate;
  bool granted_ = false;
  ReminderAuthorization proof_{};
};
class ReadingGate {
 public:
  // Main-loop check immediately before presentation, never a cached permission.
  static bool mayShow(Presentation event, PresentationState state, bool liveConsent = false);
  static ReminderPermit reminderPermit(Library&, ReminderClaim, PresentationState);
  static bool drawReminder(ReminderPermit&, ReminderDrawContext);
};
struct PositionContext {
  // Non-reused identity tokens, not hashes: reader owns exact document/path
  // lifetime and increments run at invocation, before asynchronous activation.
  uint64_t document = 0;
  uint64_t path = 0;
  uint64_t run = 0;
  bool automatic = true;  // Captured mode must still match, including manual/custom runs.
};
enum class PositionMoment { Reading, Close, Sleep };
class PositionPresentation {
 public:
  PositionPresentation(DeliveryTicket owner, PositionContext context, PositionMoment moment = PositionMoment::Reading)
      : owner(owner), context(context), live(moment == PositionMoment::Reading) {}
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
