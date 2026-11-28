// SPDX-License-Identifier: GPL-3.0-or-later
#include "DeviceBridge.h"

#include <Logging.h>
namespace booksync {
namespace {
bool same(DeviceSnapshot a, DeviceSnapshot b) {
  const auto& x = a.battery;
  const auto& y = b.battery;
  return x.percent == y.percent && x.millivolts == y.millivolts && x.supported == y.supported &&
         x.percentKnown == y.percentKnown && x.voltageKnown == y.voltageKnown && x.charging == y.charging &&
         x.chargingKnown == y.chargingKnown && x.externalPower == y.externalPower &&
         x.externalPowerKnown == y.externalPowerKnown && a.light.level == b.light.level &&
         a.light.supported == b.light.supported && a.light.known == b.light.known && a.light.on == b.light.on &&
         a.controls.orientation == b.controls.orientation && a.controls.known == b.controls.known &&
         a.controls.wifi == b.controls.wifi && a.controls.night == b.controls.night &&
         a.controls.networkBusy == b.controls.networkBusy;
}
}  // namespace
void DeviceBridge::publish(DeviceSnapshot sample) {
  if (!sample.battery.supported) sample.battery = {};
  if (sample.battery.percent > 100) sample.battery.percentKnown = false;
  if (!sample.battery.percentKnown) sample.battery.percent = 0;
  if (!sample.battery.voltageKnown) sample.battery.millivolts = 0;
  if (!sample.battery.chargingKnown) sample.battery.charging = false;
  if (!sample.battery.externalPowerKnown) sample.battery.externalPower = false;
  if (!sample.light.supported) sample.light = {};
  if (sample.light.level > 100) sample.light.known = false;
  if (!sample.light.known) {
    sample.light.level = 0;
    sample.light.on = false;
  }
  if (sample.controls.orientation > 3) sample.controls.known = false;
  if (!sample.controls.known) {
    sample.controls.orientation = 0;
    sample.controls.wifi = sample.controls.night = false;
  }
  if (current_.revision && same(sample, current_)) return;
  if (sequence_ == UINT64_MAX) {
    current_ = {};
    pending_ = false;
    return;
  }
  sample.revision = ++sequence_;
  current_ = sample;
}
CommandStatus DeviceBridge::submit(DeviceCommand command) {
  if (!command.revision || command.revision != current_.revision || pending_) return CommandStatus::Unavailable;
  uint8_t actual = 0;
  switch (command.action) {
    case DeviceAction::LightOn:
    case DeviceAction::LightLevel:
      if (!current_.light.supported) return CommandStatus::Unsupported;
      if (!current_.light.known) return CommandStatus::Unavailable;
      if (command.value > (command.action == DeviceAction::LightOn ? 1 : 100)) return CommandStatus::Unavailable;
      actual = command.action == DeviceAction::LightOn ? current_.light.on : current_.light.level;
      break;
    case DeviceAction::Wifi:
    case DeviceAction::Night:
    case DeviceAction::Orientation:
      if (!current_.controls.known || (command.action == DeviceAction::Wifi && current_.controls.networkBusy))
        return CommandStatus::Unavailable;
      if (command.value > (command.action == DeviceAction::Orientation ? 3 : 1)) return CommandStatus::Unavailable;
      actual = command.action == DeviceAction::Wifi    ? current_.controls.wifi
               : command.action == DeviceAction::Night ? current_.controls.night
                                                       : current_.controls.orientation;
      break;
    default:
      return CommandStatus::Unsupported;
  }
  command_ = command;
  if (actual == command.value && (command.action != DeviceAction::LightLevel || current_.light.on)) {
    outcome_ = DeviceOutcome::Succeeded;
    return CommandStatus::Accepted;
  }
  outcome_ = DeviceOutcome::Pending;
  pending_ = true;
  return CommandStatus::Accepted;
}
bool DeviceBridge::work(DevicePorts ports) {
  if (!pending_) return false;
  pending_ = false;
  if (command_.revision != current_.revision) {
    outcome_ = DeviceOutcome::Discarded;
    return false;
  }
  outcome_ = DeviceOutcome::Failed;
  if (!ports.apply) {
    LOG_ERR("BSD", "Native device control port unavailable");
    return false;
  }
  if (!ports.apply(ports.context, command_)) {
    LOG_ERR("BSD", "Native device control unavailable (%u)", unsigned(command_.action));
    return false;
  }
  outcome_ = DeviceOutcome::Succeeded;
  return true;
}
}  // namespace booksync
