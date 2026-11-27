#ifdef SIMULATOR
#include "BookSyncAdmissionSmokeTest.h"

#include <Logging.h>

#include <atomic>
#include <cstdlib>
#include <thread>

#include "booksync/system/NetworkPhase.h"
#include "booksync/system/Worker.h"
namespace {
std::atomic<bool> ran{false};
void require(bool ok, const char* why) {
  if (ok) return;
  LOG_ERR("F11", "%s", why);
  std::_Exit(1);
}
void work(void*) {
  auto buffer = booksync::allocateData(4096);
  require(static_cast<bool>(buffer), "bounded worker buffer");
  ran.store(true);
  // A bounded fake transport operation observes cancellation; no radio/NVS.
  while (booksync::networkAdmission().permitted()) std::this_thread::yield();
}
}  // namespace
bool verifyBookSyncAdmissionHomeTick() {
  if (!std::getenv("BOOKSYNC_ADMISSION")) return true;
  static unsigned phase = 0;
  auto& admission = booksync::networkAdmission();
  if (phase == 0) {
    require(!booksync::allocateData(booksync::maximumDataBytes + 1), "oversize job data refused");
    require(admission.begin(TAILNET_JOB_CATALOG) == booksync::AdmissionResult::Admitted, "Home admission");
#if FREEINK_FB_PSRAM
    require(admission.displayReady(), "S3 retains its PSRAM framebuffer");
#else
    require(!admission.displayReady(), "C3 owns display during network phase");
#endif
    require(booksync::startNetworkWorker(work, nullptr), "worker start");
    require(!booksync::startNetworkWorker(work, nullptr), "second worker refused");
    ++phase;
    return false;
  }
  if (phase == 1) {
    if (!ran.load()) return false;
    require(!admission.finish(), "live worker prevents teardown");
    ++phase;
    return false;
  }
  if (!booksync::pollNetworkWorker()) return false;
  if (!admission.finish()) return false;
  require(admission.displayReady() && !tailnet_service_permitted(), "joined worker restores display");
  return true;
}
void verifyBookSyncAdmissionReading() {
  if (!std::getenv("BOOKSYNC_ADMISSION")) return;
  require(booksync::networkAdmission().begin(TAILNET_JOB_CATALOG) == booksync::AdmissionResult::Reading,
          "no admission while reader is active");
  require(!booksync::startNetworkWorker(work, nullptr), "reader refuses worker");
}
void verifyBookSyncAdmissionReconnect() {
  if (!std::getenv("BOOKSYNC_ADMISSION")) return;
  auto& admission = booksync::networkAdmission();
  require(admission.begin(TAILNET_JOB_TRANSFER) == booksync::AdmissionResult::Admitted, "safe Home reconnect");
  require(admission.finish() && admission.displayReady(), "reconnect release");
  LOG_INF("F11",
          "BookSync F11 PASS: Home admission, internal worker contract, cancellation/join, reading refusal, reconnect");
}
#endif
