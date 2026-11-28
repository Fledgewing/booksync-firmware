// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/Device.h"
namespace booksync {
struct DevicePorts {
  void* context = nullptr;
  bool (*apply)(void*, DeviceCommand) = nullptr;
};
// Main-loop owned. Cached values and submission do no I/O; only work invokes
// native controls. One fixed command slot avoids allocation and silent overwrites.
class DeviceBridge {
 public:
  DeviceSnapshot snapshot() const { return current_; }
  DeviceResult result() const { return {command_, outcome_}; }
  void publish(DeviceSnapshot);
  CommandStatus submit(DeviceCommand);
  bool work(DevicePorts);
  void retire() {
    pending_ = false;
    current_ = {};
    command_ = {};
    outcome_ = DeviceOutcome::None;
  }

 private:
  DeviceSnapshot current_{};
  DeviceCommand command_{};
  uint64_t sequence_ = 0;
  bool pending_ = false;
  DeviceOutcome outcome_ = DeviceOutcome::None;
};
static_assert(sizeof(DeviceBridge) <= 80);
}  // namespace booksync
