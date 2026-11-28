// SPDX-License-Identifier: GPL-3.0-or-later
#include "Sleep.h"
namespace booksync {
SleepStep SleepCoordinator::poll(uint32_t now, bool ownershipReleased) {
  if (!waiting_) return SleepStep::Inactive;
  if (ownershipReleased) {
    waiting_ = false;
    return SleepStep::Ready;
  }
  if (static_cast<uint32_t>(now - started_) < DrainMs) return SleepStep::Draining;
  waiting_ = false;
  view_.captured = false;
  return SleepStep::Expired;
}
bool SleepCoordinator::wake(uint64_t generation) {
  if (!generation || generation != view_.generation) return false;
  waiting_ = false;
  view_.captured = false;
  return true;
}
}  // namespace booksync
