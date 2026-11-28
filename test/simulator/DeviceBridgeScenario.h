// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <CrossInkHalFrontlight.h>
#include <Logging.h>

#include <cstdio>
#include <cstdlib>

#include "CrossPointSettings.h"
#include "booksync/services/LocalLibrary.h"
#include "booksync/services/OfflineLibrary.h"
#include "booksync/system/DeviceRuntime.h"
#include "booksync/system/NetworkPhase.h"
inline void runDeviceBridgeScenario() {
  if (!std::getenv("BOOKSYNC_DEVICE_BRIDGE_TEST")) return;
  using namespace booksync;
  unsigned assertions = 0;
  auto require = [&assertions](bool result) {
    ++assertions;
    if (!result) {
      LOG_ERR("BSD", "Device bridge case %u failed", assertions);
      std::_Exit(2);
    }
  };
  const auto night = SETTINGS.screenInverted, orientation = SETTINGS.orientation;
  const auto level = SETTINGS.frontlightBrightness, lightOn = SETTINGS.frontlightOn;
  setNetworkReading(false);
  auto& lib = offlineLibrary();
  bindDeviceBridge(&deviceRuntime());
  require(lib.requestDeviceStatus());
  require(deviceStatusWanted());
  require(workDeviceRuntime({}));
  auto saved = lib.deviceStatus();
  require(saved.revision != 0 && saved.controls.known);
  require(!saved.battery.percentKnown);  // The pinned simulator has no checked telemetry.
  require(lib.deviceCommand({saved.revision, DeviceAction::Night, uint8_t(!night)}) == CommandStatus::Accepted);
  require(SETTINGS.screenInverted == night);  // Submitting never performs native work.
  require(workDeviceRuntime({}));
  require(SETTINGS.screenInverted != night);
  require(lib.deviceCommand({saved.revision, DeviceAction::Night, night}) == CommandStatus::Unavailable);
  for (uint8_t target = 0; target < 4; ++target) {
    saved = lib.deviceStatus();
    require(lib.deviceCommand({saved.revision, DeviceAction::Orientation, target}) == CommandStatus::Accepted);
    workDeviceRuntime({});
    require(SETTINGS.orientation == target);
  }
  saved = lib.deviceStatus();
  if (Frontlight.present()) {
    require(saved.light.supported && saved.light.known);
    require(lib.deviceCommand({saved.revision, DeviceAction::LightLevel, 33}) == CommandStatus::Accepted);
    workDeviceRuntime({});
    require(Frontlight.brightness() == 33 && SETTINGS.frontlightBrightness == 33);
    saved = lib.deviceStatus();
    require(lib.deviceCommand({saved.revision, DeviceAction::LightOn, 1}) == CommandStatus::Accepted);
    workDeviceRuntime({});
    require(Frontlight.isOn());
  } else {
    require(!saved.light.supported);
    require(lib.deviceCommand({saved.revision, DeviceAction::LightLevel, 50}) == CommandStatus::Unsupported);
    require(lib.deviceCommand({saved.revision, DeviceAction::LightOn, 1}) == CommandStatus::Unsupported);
  }
  setNetworkReading(true);
  workDeviceRuntime({});
  saved = lib.deviceStatus();
  require(saved.controls.networkBusy);
  require(lib.deviceCommand({saved.revision, DeviceAction::Wifi, 1}) == CommandStatus::Unavailable);
  saved = lib.deviceStatus();
  require(lib.deviceCommand({saved.revision, DeviceAction::Orientation, orientation}) == CommandStatus::Accepted);
  workDeviceRuntime({});
  bindDeviceBridge(nullptr);
  retireDeviceRuntime();
  require(!lib.requestDeviceStatus());
  require(!lib.deviceStatus().revision);
  SETTINGS.screenInverted = night;
  SETTINGS.orientation = orientation;
  SETTINGS.frontlightBrightness = level;
  SETTINGS.frontlightOn = lightOn;
  Frontlight.setBrightness(level);
  Frontlight.setOn(lightOn);
  LOG_INF("BSD", "BookSync device bridge PASS: assertions=%u fixture-ports=0 paint-io=0", assertions);
  std::fflush(nullptr);
  std::_Exit(0);
}
