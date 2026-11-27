// SPDX-License-Identifier: GPL-3.0-or-later
#include "NetworkPhase.h"

#include <HalDisplay.h>
#include <Logging.h>

#include "Worker.h"
#include "activities/Activity.h"
#ifndef SIMULATOR
#include <WiFi.h>

#include "Tailnet.h"
#endif
namespace booksync {
namespace {
MemorySnapshot memory(void*) { return memorySnapshot(); }
bool park(void*) {
  RenderLock lock;
#ifdef SIMULATOR
  // The pinned emulator supplies its own HAL; keep its storage and test faults
  // through AdmissionPorts rather than editing the simulator dependency.
  return true;
#else
  return display.releaseNetworkBuffers();
#endif
}
bool restore(void*) {
  RenderLock lock;
#ifdef SIMULATOR
  display.clearScreen();
  const bool ready = true;
#else
  const bool ready = display.restoreNetworkBuffers();
#endif
  if (ready)
    activityManager.requestUpdate();
  else
    LOG_ERR("ADMISSION", "Framebuffer restore deferred; display remains leased");
  return ready;
}
bool stop(void*) {
  if (!pollNetworkWorker()) return false;
#ifndef SIMULATOR
  if (!tailnet_service_stop_and_wait(100)) return false;
  // No Wi-Fi teardown under live MicroLink tasks. The esp_wifi_stop wrap also
  // checks transport shutdown; false leaves phase/display ownership intact.
  if (!WiFi.mode(WIFI_OFF)) {
    LOG_ERR("ADMISSION", "Wi-Fi teardown incomplete");
    return false;
  }
#endif
  return true;
}
}  // namespace
Admission& networkAdmission() {
#if FREEINK_FB_PSRAM
  constexpr bool needsDisplay = false;
#else
  constexpr bool needsDisplay = true;
#endif
  static Admission admission({nullptr, memory, park, restore, stop}, needsDisplay);
  return admission;
}
void setNetworkReading(bool reading) { networkAdmission().setReading(reading); }
bool networkDisplayReady() { return networkAdmission().displayReady(); }
void networkAdmissionTick() {
  pollNetworkWorker();
  if (networkAdmission().finishing()) networkAdmission().finish();
}
}  // namespace booksync
