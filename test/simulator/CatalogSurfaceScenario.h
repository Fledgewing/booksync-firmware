// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "../host/catalog_surface/SceneFixture.h"
#include "activities/Activity.h"
#include "booksync/system/CatalogScreen.h"
#include "components/UITheme.h"
extern GfxRenderer renderer;
extern MappedInputManager mappedInputManager;
namespace catalogSurfaceScenario {
inline bool tick() {
  const auto* output = std::getenv("BOOKSYNC_CATALOG_CAPTURES");
  if (!output) return false;
  mappedInputManager.simulatorClearInputFrame();
  static catalogFixture::SceneLibrary library;
  static unsigned frame = 0, scene = 0;
  if (++frame == 1) {
    library.configureScene(scene);
    booksync::showCatalogScene(renderer, mappedInputManager, library);
  }
  if (frame == 5) {
    RenderLock lock;
    auto* controller = booksync::catalogSceneController();
    if (!controller) std::_Exit(1);
    catalogFixture::arrange(*controller, library);
  }
  // Exercise the actual profile input route for the group header, then Back.
  if (scene == 6 && frame == 8) {
    bool touch = false;
#if CROSSINK_APP_CAP_TOUCH
    touch = mappedInputManager.hasTouch();
    if (touch) {
      const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
      const auto header = UITheme::getHeaderRect(renderer, UITheme::getInstance().getMetrics().headerHeight, safe);
      const int x = safe.x + safe.width - 80, y = header.y + header.height + 77;
      mappedInputManager.simulatorInjectTouchDown(x, y);
      mappedInputManager.simulatorInjectTouchRelease(x, y);
    }
#endif
    if (!touch) {
      RenderLock lock;
      auto* controller = booksync::catalogSceneController();
      controller->selectCatalogControl(controller->catalogVisibleRows() + 3);
      mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Confirm);
    }
  }
  if (scene == 6 && frame == 11) {
    RenderLock lock;
    auto* controller = booksync::catalogSceneController();
    if (controller->catalogPanel() != booksync::CatalogSurface::Panel::Books || !library.wanted.filter.authors.count)
      std::_Exit(5);
    mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Back);
  }
  if (scene == 6 && frame == 14) {
    RenderLock lock;
    auto* controller = booksync::catalogSceneController();
    if (controller->catalogPanel() != booksync::CatalogSurface::Panel::Groups ||
        controller->catalogFocus() != controller->catalogVisibleRows() + 3)
      std::_Exit(6);
  }
  if (frame < 25) return true;
  const auto reads = library.reads;
  if (activityManager.requestUpdateAndWait() != RequestUpdateResult::Rendered) std::_Exit(2);
  {
    RenderLock lock;
    if (library.reads != reads || library.connects) {
      LOG_ERR("BSC", "Catalog paint performed Library work or automatic connection");
      std::_Exit(3);
    }
    std::filesystem::create_directories(output);
    const auto path = std::filesystem::path(output) / (std::string(catalogFixture::names[scene]) + ".pgm");
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) std::_Exit(4);
    std::fprintf(file, "P5\n%d %d\n255\n", renderer.getScreenWidth(), renderer.getScreenHeight());
    for (int y = 0; y < renderer.getScreenHeight(); ++y)
      for (int x = 0; x < renderer.getScreenWidth(); ++x) std::fputc(renderer.isPixelBlack(x, y) ? 0 : 255, file);
    std::fclose(file);
  }
  frame = 0;
  if (++scene == 30) {
    LOG_INF("BSC", "BookSync F36a PASS: 30 approved states, no paint work or automatic connection; controller=%u",
            unsigned(sizeof(booksync::CatalogSurface)));
    std::fflush(nullptr);
    std::_Exit(0);
  }
  return true;
}
}  // namespace catalogSurfaceScenario
