// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/Delivery.h"

namespace booksync {
// Main-loop owned. Workers post immutable results; they never call consumers.
// Callbacks may retire, but must not destroy the ticket during poll().
// No job handle, cancellation API, allocation, renderer, or account-state payload.
enum class DeliveryResult { Pending, Ready, Unavailable, Invalid, Canceled };
enum class DeliveryPolicy { Owner, Device };
struct DeliveryCallback {
  void (*call)(void*, DeliveryResult) = nullptr;
  void* context = nullptr;
};
class DeliveryOwner {
 public:
  DeliveryOwner() = default;
  DeliveryOwner(const DeliveryOwner&) = delete;
  DeliveryOwner& operator=(const DeliveryOwner&) = delete;
  // Call for every observed owner transition (including sign-out). Same-owner
  // reloads preserve epoch; A -> B -> A does not. On exhaustion the caller
  // must stop admission (false), never reuse an epoch.
  bool adopt(uint32_t owner);
  DeliveryTicket current() const { return value; }

 private:
  DeliveryTicket value{};
};
class Delivery {
 public:
  static constexpr uint64_t PendingLimitMs = 120000;
  Delivery(DeliveryTicket owner, uint64_t startedMs, DeliveryPolicy policy, DeliveryCallback presentation,
           DeliveryCallback deviceEffect = {});
  Delivery(const Delivery&) = delete;
  Delivery& operator=(const Delivery&) = delete;
  // Explicit retirement is silent. Device effects have device-owned context and
  // keep polling after view retirement; ordinary tickets stop immediately.
  void retire();
  bool polling() const { return live; }
  void poll(uint64_t nowMs, DeliveryResult result, DeliveryTicket current, bool eligible = true);

 private:
  void settle(DeliveryResult result);
  DeliveryTicket owner;
  uint64_t startedMs;
  DeliveryPolicy policy;
  DeliveryCallback presentation;
  DeliveryCallback effect;
  bool live = true;
};
}  // namespace booksync
