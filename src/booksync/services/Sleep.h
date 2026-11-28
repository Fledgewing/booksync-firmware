// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/Sleep.h"
namespace booksync {
enum class SleepStep { Inactive, Draining, Ready, Expired };
// Main-loop owner. Capture is synchronous and local, before network drain
// begins. No worker, allocation, sync writer or forced task deletion lives here.
class SleepCoordinator {
 public:
  static constexpr uint32_t DrainMs = 2000;
  template <typename Capture>
  bool begin(uint32_t now, Capture capture) {
    if (waiting_) return false;
    capture();
    ++view_.generation;
    view_.captured = true;
    started_ = now;
    waiting_ = true;
    return true;
  }
  SleepStep poll(uint32_t now, bool ownershipReleased);
  // A late completion from an older sleep must not retire a newer pass.
  bool wake(uint64_t generation);
  SleepView sleepState() const { return view_; }
  bool waiting() const { return waiting_; }
 private:
  SleepView view_{};
  uint32_t started_ = 0;
  bool waiting_ = false;
};
static_assert(sizeof(SleepCoordinator) <= 32);
}  // namespace booksync
