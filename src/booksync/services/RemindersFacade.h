// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <optional>

#include "Delivery.h"
#include "Reminders.h"
namespace booksync {
// Main-loop-owned command slot. UI calls enqueue only; worker drains once outside
// paint. No callbacks, jobs or filesystem handles enter the public interface.
struct ReminderSleepPort {
  bool (*request)(void*) = nullptr;
  void* context = nullptr;
};
class RemindersFacade {
 public:
  explicit RemindersFacade(Reminders& service) : service_(service) {}
  void work(const ReminderNow&, bool input = false);
  void observe(const ReminderNow& now, bool input = false) { service_.activity(now, input); }
  bool hasReminderCommand() const { return pending_ != Pending::None || !initialized_; }
  void list(ReminderList&) const;
  ReminderResult mutate(const ReminderMutation&);
  ReminderResult claim(const ReminderDue&, uint64_t);
  ReminderResult respond(uint64_t, uint64_t, ReminderAction);
  bool retry();
  bool claimCommand() const { return pending_ == Pending::Claim; }
  bool storageCommand() const {
    return !initialized_ || pending_ == Pending::Mutation ||
           (pending_ == Pending::Claim && due_.kind == ReminderKind::Time) ||
           (pending_ == Pending::Response &&
            (action_ == ReminderAction::Snooze10 || action_ == ReminderAction::TurnOff));
  }
  bool nativeSleepCommand() const { return pending_ == Pending::Response && action_ == ReminderAction::Sleep; }
  ReminderAuthorization authorize(ReminderClaim claim) { return service_.authorize(claim, now_); }
  ReminderResult result() const { return result_; }
  ReminderEvents events() const { return events_; }
  bool due(ReminderDue& out) const;
  void retire(uint64_t owner);
  void configureSleep(ReminderSleepPort port) { sleepPort_ = port; }
  void sleepAvailable(bool available) { sleepAvailable_ = available; }
  using ListFormatter = void (*)(ReminderList&);
  void configureListFormatter(ListFormatter formatter) { listFormat_ = formatter; }
  using Formatter = void (*)(ReminderPresentation&, const ReminderNow&);
  void configureFormatter(Formatter format) { format_ = format; }
  ReminderPresentation presentation(ReminderClaim) const;
  bool sleepAvailable() const { return sleepAvailable_ && sleepPort_.request; }

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
  bool initialized_ = false, ready_ = false, retireResponse_ = false, sleepAvailable_ = false;
  ReminderSleepPort sleepPort_{};
  Formatter format_ = nullptr;
  ListFormatter listFormat_ = nullptr;
  std::optional<Delivery> delivery_;
  uint64_t queued_ = 0;
  void beginDeviceDelivery();
  static void adoptDeviceResult(void*, DeliveryResult);
};
}  // namespace booksync
