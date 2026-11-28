// SPDX-License-Identifier: GPL-3.0-or-later
#include "BatteryRuntime.h"

#include <AppCapabilities.h>
#if CROSSINK_APP_CAP_BATTERY_HISTORY

#include <Arduino.h>
#include <CrossInkHalFrontlight.h>
#include <HalPowerManager.h>
#include <Logging.h>
#include <TrustedTime.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#ifndef SIMULATOR
#include <WiFi.h>
#include <esp_attr.h>
#include <esp_system.h>
#endif
#include "NetworkPhase.h"
#include "StatsIdentity.h"
#include "activities/Activity.h"
#include "activities/RenderLock.h"
#include "booksync/services/BatteryCapture.h"
#include "booksync/storage/BatteryJournal.h"
namespace booksync {
namespace {
// Static ownership spans reader/Home activities and failed writes; fixed <1 KiB,
// with no heap failure on sleep. All access is serialized by the main task.
char directory[128]{};
storage::BatteryJournal history(directory);
battery::Capture producer;
#ifndef SIMULATOR
RTC_DATA_ATTR uint32_t retainedSleep = 0;
#endif
bool bootObserved = false, continuedSleep = false;
void observeBoot() {
  if (bootObserved) return;
  bootObserved = true;
#ifndef SIMULATOR
  continuedSleep = esp_reset_reason() == ESP_RST_DEEPSLEEP && retainedSleep == 0x42534231;
  retainedSleep = 0;  // An unrecorded/failed boot can never inherit a prior sleep proof.
#endif
}
uint32_t polledAt = 0;
bool prepared = false, recovering = false, failed = false, polled = false;
bool append(void*, const battery::Record& record) {
  const auto result = history.append(record);
  if (result == storage::JournalResult::Ok) return true;
  if (result == storage::JournalResult::Capacity) failed = true;
  LOG_ERR("BSBT", "Native battery observation retained in memory: %u", unsigned(result));
  return false;
}
bool prepare() {
  if (failed) return false;
  if (prepared) return true;
  if (!recovering) {
    // Read the immutable identity directly. StatsStorage's migration helper
    // shares a failure latch with reader counters and must not be called here.
    char readerId[32]{};
    if (!StatsIdentity::readerId(readerId, sizeof(readerId)) ||
        !battery::measurementDirectory(readerId, directory, sizeof(directory))) {
      LOG_ERR("BSBT", "Native battery origin unavailable");
      failed = true;
      return false;
    }
    const auto result = history.startRecovery(true);
    if (result != storage::JournalResult::Incomplete) {
      failed = true;
      return false;
    }
    recovering = true;
  }
  const auto result = history.recoverStep();
  if (result == storage::JournalResult::Incomplete) return false;
  recovering = false;
  prepared = result == storage::JournalResult::Ok;
  failed = !prepared;
  if (failed) LOG_ERR("BSBT", "Native battery history retained; recovery unavailable");
  return prepared;
}
battery::CaptureInput observe() {
  battery::CaptureInput in;
  in.uptime = millis() / 1000;
  const auto epoch = std::time(nullptr);
  in.clockKnown = trustedtime::clockStatus() != trustedtime::ClockSource::Unknown && epoch >= 946684800 &&
                  static_cast<uint64_t>(epoch) <= UINT32_MAX;
  if (in.clockKnown) in.epoch = static_cast<uint32_t>(epoch);
#ifndef SIMULATOR
  const auto status = powerManager.batteryStatus();
  in.known = status.supported && status.percentageKnown && status.externalPowerKnown;
  in.pct = status.percentage;
  in.mv = status.millivoltsKnown ? status.millivolts : 0;
  in.usb = status.externalPower;
  in.radio = WiFi.getMode() != WIFI_MODE_NULL;
  in.crash = !continuedSleep;
#endif
  in.light = Frontlight.present() && Frontlight.isOn();
  in.lightLevel = Frontlight.present() ? Frontlight.brightness() : 0;
  if (activityManager.isReaderActivity())
    in.content = activityManager.hasActivityNamed("XtcReader") ? battery::CONTENT_XTC : battery::CONTENT_TEXT;
  return in;
}
bool batteryCaptureAdmitted() {
  return Storage.ready() && !activityManager.requiresExclusiveStorageLoop() && !networkAdmission().busy() &&
         networkDisplayReady() && tailnet_service_idle();
}
}  // namespace
void workBatteryCapture() {
  observeBoot();
  if (!batteryCaptureAdmitted()) {
    producer.pause();
    return;
  }
  const uint32_t now = millis();
  if (polled && now - polledAt < 1000 && !recovering) return;
  RenderLock lock(RenderLock::Mode::Try);
  if (!lock.ownsLock()) return;
  // Recovery itself advances one segment per existing loop, not per second.
  if (!prepare()) return;
  polled = true;
  polledAt = now;
  producer.tick(observe(), {nullptr, append});
}
void captureBatterySleep() {
  observeBoot();
  if (!batteryCaptureAdmitted() || !RenderLock::heldByCurrentTask()) return;
  // A cold boot sleeping before recovery completes supplies no standby proof;
  // the next boot will see the previous awake/gap tail and fail closed.
  if (!prepared) return;
  const auto input = observe();
  // Retrying an uncertain row and inserting a gap can precede the sleep row.
  // Each publication is bounded to one tail segment; no unbounded retry loop.
  for (unsigned i = 0; i < 3 && !failed; ++i) {
    producer.tick(input, {nullptr, append}, battery::KIND_SLEEP);
    if (producer.sleeping()) {
#ifndef SIMULATOR
      retainedSleep = 0x42534231;
#endif
      break;
    }
    if (producer.pending()) break;
  }
}
}  // namespace booksync

#else
namespace booksync {
void workBatteryCapture() {}
void captureBatterySleep() {}
}  // namespace booksync
#endif
