// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "I18n.h"
#include "components/BookSyncPresentation.h"
extern GfxRenderer renderer;
extern MappedInputManager mappedInputManager;
namespace presentationKitScenario {
inline bool tick() {
  const char* out = std::getenv("BOOKSYNC_KIT_CAPTURES");
  if (!out) return false;
  namespace kit = BookSyncPresentation;
  RenderLock lock;
  const auto f = kit::frame(renderer, mappedInputManager.hasTouch(), gpio.deviceIsX3());
  const auto c = f.content();
  const char* names[] = {"header-footer", "rows", "actions-stacked", "actions-pair", "type", "sections"};
  std::filesystem::create_directories(out);
  for (unsigned i = 0; i < 6; ++i) {
    renderer.clearScreen();
    kit::header(renderer, f, tr(STR_ACCOUNT_YOUR_SERVER), i == 0 ? "+" : nullptr);
    if (i == 1) {
      kit::row(renderer, f, f.row(c.y), tr(STR_ACCOUNT_ACCOUNT), tr(STR_ACCOUNT_NOT_SIGNED_IN), true, true);
      kit::row(renderer, f, f.row(c.y + f.unit(62)), tr(STR_ACCOUNT_SYNC), tr(STR_ACCOUNT_AVAILABLE), false, true);
    } else if (i == 2 || i == 3) {
      const char* labels[] = {tr(STR_ACCOUNT_SIGN_IN), tr(STR_CANCEL)};
      kit::drawActions(renderer, f, f.actions(c.y + f.unit(100), 2, i == 2), labels, 0, 0);
    } else if (i == 4) {
      int y = kit::paragraph(renderer, f, Rect{c.x, c.y, c.width, f.unit(80)}, tr(STR_ACCOUNT_YOUR_SERVER),
                             kit::Type::Title);
      y = kit::paragraph(renderer, f, Rect{c.x, y + f.unit(20), c.width, f.unit(100)}, tr(STR_ACCOUNT_OUT_BODY));
      kit::text(renderer, f, f.row(y + f.unit(20)), tr(STR_ACCOUNT_SIGN_IN), kit::Type::Label);
    } else if (i == 5) {
      kit::section(renderer, f, f.row(c.y, 28), tr(STR_UPGRADE_CARRIED));
      kit::paragraph(renderer, f, Rect{c.x, c.y + f.unit(36), c.width, f.unit(100)}, tr(STR_UPGRADE_RETAINED));
    }
    kit::footer(renderer, f, tr(STR_ACCOUNT_YOUR_SERVER));
    const auto path = std::filesystem::path(out) / (std::string(names[i]) + ".pgm");
    FILE* image = std::fopen(path.c_str(), "wb");
    if (!image) std::_Exit(41);
    std::fprintf(image, "P5\n%d %d\n255\n", renderer.getScreenWidth(), renderer.getScreenHeight());
    for (int y = 0; y < renderer.getScreenHeight(); ++y)
      for (int x = 0; x < renderer.getScreenWidth(); ++x) std::fputc(renderer.isPixelBlack(x, y) ? 0 : 255, image);
    std::fclose(image);
  }
  std::puts("BookSync FK kit PASS: 6 component renders");
  std::fflush(nullptr);
  std::_Exit(0);
}
}  // namespace presentationKitScenario
