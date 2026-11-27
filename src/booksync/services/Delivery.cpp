// SPDX-License-Identifier: GPL-3.0-or-later
#include "Delivery.h"

#include <limits>

namespace booksync {
bool DeliveryOwner::adopt(uint32_t owner) {
  if (value.epoch == std::numeric_limits<uint64_t>::max()) return false;
  if (value.owner != owner) {
    value.owner = owner;
    ++value.epoch;
  }
  return true;
}
Delivery::Delivery(DeliveryTicket owner, uint64_t startedMs, DeliveryPolicy policy, DeliveryCallback presentation,
                   DeliveryCallback deviceEffect)
    : owner(owner),
      startedMs(startedMs),
      policy(policy),
      presentation(presentation),
      effect(policy == DeliveryPolicy::Device ? deviceEffect : DeliveryCallback{}) {}
void Delivery::retire() {
  presentation = {};
  if (!effect.call) live = false;
}
void Delivery::settle(DeliveryResult result) {
  if (!live) return;
  const auto adopt = effect;
  live = false;
  effect = {};
  // Detach before callbacks, including synchronous rejection/reentrant retirement.
  // Device effects publish state only; presentation is dispatched separately by
  // the caller, which must check ReadingGate immediately before any display work.
  if (adopt.call) adopt.call(adopt.context, result);
  const auto done = presentation;
  presentation = {};
  if (done.call) done.call(done.context, result);
}
void Delivery::poll(uint64_t nowMs, DeliveryResult result, DeliveryTicket current, bool eligible) {
  if (!live) return;
  if (!eligible ||
      (policy == DeliveryPolicy::Owner && (owner.owner != current.owner || owner.epoch != current.epoch))) {
    settle(DeliveryResult::Canceled);
    return;
  }
  if (result != DeliveryResult::Pending) {
    settle(result);
    return;
  }
  // Monotonic milliseconds supplied by runtime. Avoid overflow on deadline add.
  if (nowMs >= startedMs && nowMs - startedMs >= PendingLimitMs) settle(DeliveryResult::Unavailable);
}
}  // namespace booksync
