#ifdef SIMULATOR
#include "BookSyncHomeSmokeTest.h"

#include <ArduinoJson.h>
#include <Logging.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "booksync/system/Bootstrap.h"
#include "components/UITheme.h"

extern GfxRenderer renderer;
extern MappedInputManager mappedInputManager;

namespace {
void require(bool passed, const char* reason) {
  if (passed) return;
  LOG_ERR("F04", "%s", reason);
  std::_Exit(1);
}
void verifyStylingPersistence() {
  RenderLock lock;
  JsonDocument original;
  SETTINGS.toJson(original);
  for (uint8_t theme = 0; theme < CrossPointSettings::UI_THEME_COUNT; ++theme) {
    SETTINGS.uiTheme = theme;
    SETTINGS.swapLibraryFileBrowser = theme % 2;
    JsonDocument saved;
    SETTINGS.toJson(saved);
    SETTINGS.uiTheme = CrossPointSettings::LYRA;
    SETTINGS.swapLibraryFileBrowser = 0;
    SETTINGS.fromJson(saved.as<JsonVariantConst>());
    require(SETTINGS.uiTheme == theme && SETTINGS.swapLibraryFileBrowser == theme % 2,
            "Retired Home selectors lost persisted reader styling");
  }
  SETTINGS.fromJson(original.as<JsonVariantConst>());
  UITheme::getInstance().reload();
}
void touchAction(bool down) {
#if CROSSINK_APP_CAP_TOUCH
  const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const auto header = UITheme::getHeaderRect(renderer, UITheme::getInstance().getMetrics().headerHeight, safe);
  const int x = safe.x + safe.width / 2;
  const int y = header.y + header.height + 32;
  if (down)
    mappedInputManager.simulatorInjectTouchDown(x, y);
  else
    mappedInputManager.simulatorInjectTouchRelease(x, y);
#else
  (void)down;
#endif
}
bool readerReady() {
  if (!activityManager.isCurrentActivityNamed("EpubReader")) return false;
  RenderLock lock;
  const auto info = activityManager.getScreenshotInfo();
  return info.currentPage > 0 && info.totalPages >= info.currentPage;
}
void capture(const char* name) {
  const char* output = std::getenv("BOOKSYNC_HOME_CAPTURES");
  if (!output) return;
  require(activityManager.requestUpdateAndWait() == RequestUpdateResult::Rendered, "Capture paint was rejected");
  RenderLock lock;
  std::filesystem::create_directories(output);
  const auto path = std::filesystem::path(output) / (std::string(name) + ".pgm");
  FILE* image = std::fopen(path.c_str(), "wb");
  require(image != nullptr, "Cannot write Home capture");
  std::fprintf(image, "P5\n%d %d\n255\n", renderer.getScreenWidth(), renderer.getScreenHeight());
  for (int y = 0; y < renderer.getScreenHeight(); ++y) {
    for (int x = 0; x < renderer.getScreenWidth(); ++x) std::fputc(renderer.isPixelBlack(x, y) ? 0 : 255, image);
  }
  std::fclose(image);
}
}  // namespace

bool runBookSyncHomeSmokeTestTick() {
  const char* book = std::getenv("BOOKSYNC_HOME_BOOK");
  if (!book) return false;
  mappedInputManager.simulatorClearInputFrame();
  static unsigned step = 0;
  static unsigned settled = 0;
  // Let real main-loop input, navigation and the render task complete between
  // observations. Input is injected into the same mapped release path as buttons.
  if (++settled < (step == 4 ? 1u : 30u)) return true;
  settled = 0;
  switch (step++) {
    case 0:
      verifyStylingPersistence();
      require(activityManager.isHomeActivity(), "Offline cold boot did not reach owned Home");
      require(!booksync::homeLibrary().snapshot().canOpenLastBook, "Empty SD claimed a recent book");
      require(booksync::homeLibrary().submit(booksync::Intent::OpenLastBook) == booksync::CommandStatus::Unavailable,
              "Empty Home accepted an open");
      capture("home-empty");
      APP_STATE.openEpubPath = book;
      activityManager.goHome();
      break;
    case 1:
      require(activityManager.isHomeActivity(), "Seeded Home was not entered");
      require(booksync::homeLibrary().snapshot().canOpenLastBook, "Saved local book unavailable offline");
      capture("home-local");
      mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Confirm);
      break;
    case 2:
      if (!readerReady()) {
        --step;  // Bounded by the external scenario timeout, never retry a run.
        break;
      }
      require(activityManager.getCurrentBookPath() == book, "Home opened a different book");
      capture("home-reader");
      mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Back);
      break;
    case 3:
      if (!activityManager.isHomeActivity()) {
        --step;  // Native section cancellation may finish after the input frame.
        break;
      }
      require(booksync::homeLibrary().snapshot().canOpenLastBook, "Back lost the local open action");
      capture("home-back");
      if (mappedInputManager.hasTouch())
        touchAction(true);
      else
        step = 6;
      break;
    case 4:
      touchAction(false);
      break;
    case 5:
      if (!readerReady()) {
        --step;
        break;
      }
      require(activityManager.getCurrentBookPath() == book, "Touch opened a different book");
      mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Back);
      break;
    case 6:
      if (!activityManager.isHomeActivity()) {
        --step;
        break;
      }
      // A stale/missing local path must never trigger a scan or network fallback.
      APP_STATE.openEpubPath = "/missing.epub";
      activityManager.goHome();
      break;
    case 7:
      require(activityManager.isHomeActivity() && !booksync::homeLibrary().snapshot().canOpenLastBook,
              "Missing saved file remained openable");
      LOG_INF("F04", "Native simulator heap: free=%u largest=%u (stub values, not device acceptance)",
              ESP.getFreeHeap(), ESP.getMaxAllocHeap());
      LOG_INF("F04", "BookSync F04 PASS: offline empty Home, local open, native reader Back, missing file");
      std::_Exit(0);
  }
  return true;
}
#endif
