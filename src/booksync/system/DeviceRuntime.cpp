// SPDX-License-Identifier: GPL-3.0-or-later
#include "DeviceRuntime.h"

#include <CrossInkHalFrontlight.h>
#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "NetworkPhase.h"
#include "activities/Activity.h"
#include "activities/reader/ReaderUtils.h"
#ifndef SIMULATOR
#include <WiFi.h>
#endif
extern GfxRenderer renderer;
namespace booksync {
namespace {
DeviceBridge bridge;
DevicePorts activityPorts{};
uint32_t lastSample = 0;
bool sampled = false;
DeviceBattery batteryValue{};
bool apply(void*, DeviceCommand command) {
  if (command.action == DeviceAction::Wifi)
    return networkAdmission().storageIdle() && activityPorts.apply &&
           activityPorts.apply(activityPorts.context, command);
  switch (command.action) {
    case DeviceAction::Night:
      SETTINGS.screenInverted = command.value;
      break;
    case DeviceAction::Orientation:
      SETTINGS.orientation = command.value;
      ReaderUtils::applyOrientation(renderer, command.value);
      break;
    case DeviceAction::LightOn:
      if (!Frontlight.present()) return false;
      Frontlight.setOn(command.value != 0);
      SETTINGS.frontlightOn = command.value;
      break;
    case DeviceAction::LightLevel:
      if (!Frontlight.present()) return false;
      // Match the existing native brightness panel/swipe, including wake from off.
      Frontlight.setBrightness(command.value);
      Frontlight.setOn(true);
      SETTINGS.frontlightBrightness = command.value;
      SETTINGS.frontlightOn = 1;
      break;
    default:
      return false;
  }
  activityManager.persistGlobalSettings();
  activityManager.requestUpdate();
  return true;
}
void sample() {
  DeviceSnapshot value{};
  const uint32_t now = millis();
  if (!sampled || now - lastSample >= 30000u) {
    sampled = true;
    lastSample = now;
#ifndef SIMULATOR
    const auto status = powerManager.batteryStatus();
    batteryValue = {status.percentage,      status.millivolts,      status.supported,
                    status.percentageKnown, status.millivoltsKnown, status.charging,
                    status.chargingKnown,   status.externalPower,   status.externalPowerKnown};
#else
    // Simulator cards inject typed fixtures. Its legacy percentage-only stub
    // cannot prove a successful zero reading, so runtime telemetry stays absent.
    batteryValue = {};
#endif
  }
  value.battery = batteryValue;
  value.light = {Frontlight.brightness(), Frontlight.present(), Frontlight.present(), Frontlight.isOn()};
  value.controls.known = true;
  value.controls.orientation = SETTINGS.orientation;
  value.controls.night = SETTINGS.screenInverted != 0;
  value.controls.networkBusy = !networkAdmission().storageIdle();
#ifndef SIMULATOR
  value.controls.wifi = WiFi.getMode() != WIFI_MODE_NULL;
#endif
  bridge.publish(value);
}
}  // namespace
DeviceBridge& deviceRuntime() { return bridge; }
bool workDeviceRuntime(DevicePorts navigation) {
  const auto before = bridge.snapshot().revision;
  const auto resultBefore = bridge.result().outcome;
  sample();
  activityPorts = navigation;
  const bool applied = bridge.work({nullptr, apply});
  activityPorts = {};
  if (applied) sample();
  return applied || before != bridge.snapshot().revision || resultBefore != bridge.result().outcome;
}
void retireDeviceRuntime() {
  bridge.retire();
  activityPorts = {};
  sampled = false;
}
}  // namespace booksync
