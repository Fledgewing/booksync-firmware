// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Reminders.h"
namespace booksync {
// Main-loop-owned command slot. UI calls enqueue only; worker drains once outside
// paint. No callbacks, jobs or filesystem handles enter the public interface.
class RemindersFacade {
 public:
  explicit RemindersFacade(Reminders& service) : service_(service) {}
  void work(const ReminderNow&, bool input = false);
  void list(ReminderList&) const;
  ReminderResult mutate(const ReminderMutation&);
  ReminderResult claim(const ReminderDue&, uint64_t);
  ReminderResult respond(uint64_t, uint64_t, ReminderAction);
  bool retry();
  bool sleepAvailable() const { return false; }
  ReminderResult result() const { return result_; }
  ReminderEvents events() const { return events_; }
  bool due(ReminderDue& out) const;
  void retire(uint64_t owner);

 private:
  enum class Pending : uint8_t { None, Mutation, Claim, Response } pending_ = Pending::None;
  Reminders& service_;
  ReminderMutation mutation_{};
  ReminderDue due_{};
  ReminderNow now_{};
  ReminderResult result_{};
  ReminderEvents events_{};
  uint64_t owner_ = 0, token_ = 0;
  ReminderAction action_ = ReminderAction::GotIt;
  ReminderStorage storage_ = ReminderStorage::Unreadable;
  bool initialized_ = false, ready_ = false;
};
}  // namespace booksync
