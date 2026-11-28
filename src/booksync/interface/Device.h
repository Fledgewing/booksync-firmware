// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Commands.h"
namespace booksync {
struct DeviceBattery {
  uint16_t percent = 0, millivolts = 0;
  bool supported = false, percentKnown = false, voltageKnown = false;
  bool charging = false, chargingKnown = false, externalPower = false, externalPowerKnown = false;
};
struct DeviceLight {
  uint8_t level = 0;
  bool supported = false, known = false, on = false;
};
struct DeviceControls {
  uint8_t orientation = 0;
  bool known = false, wifi = false, night = false, networkBusy = false;
};
struct DeviceSnapshot {
  uint64_t revision = 0;
  DeviceBattery battery{};
  DeviceLight light{};
  DeviceControls controls{};
};
enum class DeviceAction : uint8_t { Wifi, Night, Orientation, LightOn, LightLevel };
struct DeviceCommand {
  uint64_t revision = 0;
  DeviceAction action = DeviceAction::Wifi;
  uint8_t value = 0;
};
enum class DeviceOutcome : uint8_t { None, Pending, Succeeded, Failed, Discarded };
struct DeviceResult {
  DeviceCommand command{};
  DeviceOutcome outcome = DeviceOutcome::None;
};
static_assert(sizeof(DeviceSnapshot) <= 40);
}  // namespace booksync
