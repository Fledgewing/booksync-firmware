// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "../host/catalog_surface/SceneFixture.h"
#include "activities/Activity.h"
#include "booksync/system/CatalogScreen.h"
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
