// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "activities/Activity.h"
#include "booksync/storage/LayoutStore.h"
#include "booksync/system/Bootstrap.h"
#include "components/UITheme.h"
#include "fontIds.h"
extern GfxRenderer renderer;
extern MappedInputManager mappedInputManager;
namespace editorScenario {
using namespace booksync;
class Fixture final : public Library {
 public:
  storage::LayoutStore store{{"/editor", "/editor.tmp", "/editor.bak"}, Layout::deviceClass()};
  LayoutEditor worker{};
  HomePage page{};
  LayoutEdit request{};
  LayoutEditResult reply{};
  bool queued = false;
  mutable unsigned paints = 0;
  unsigned writes = 0;
  Capabilities capabilities() const override { return {}; }
  Snapshot snapshot() const override { return {}; }
  HomePage homePage() const override { return page; }
  bool requestHomePage(uint8_t) override { return true; }
  bool requestBatch(WindowBatchRequest) override { return true; }
  CommandStatus submit(Intent) override { return CommandStatus::Unavailable; }
  CommandStatus editLayout(LayoutEdit edit) override {
    if (queued) return CommandStatus::Unavailable;
    request = edit;
    queued = true;
    reply.status = LayoutEditStatus::Pending;
    return CommandStatus::Accepted;
  }
  LayoutEditResult layoutEditResult() const override { return reply; }
  void work() {
    if (!queued) return;
    queued = false;
    ++writes;
    reply.status = store.edit(worker, request);
    if (!store.project(0, page)) std::_Exit(11);
    reply.revision = page.revision;
  }
};
inline void capture(const char* directory, const char* name) {
  if (activityManager.requestUpdateAndWait() != RequestUpdateResult::Rendered) std::_Exit(12);
  RenderLock lock;
  if (!std::strcmp(name, "reset-keep-focused")) {
    const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const auto header = UITheme::getHeaderRect(renderer, UITheme::getInstance().getMetrics().headerHeight, safe);
    const int line = renderer.getLineHeight(UI_10_FONT_ID);
    const int x = safe.x + 25, y = header.y + header.height + line * 3 + 30;
    // Keep has a three-pixel focus border; Reset has a one-pixel outline.
    if (!renderer.isPixelBlack(x, y) || renderer.isPixelBlack(x, y + line + 26)) std::_Exit(20);
  }
  std::filesystem::create_directories(directory);
  auto path = std::filesystem::path(directory) / (std::string(name) + ".pgm");
  FILE* file = std::fopen(path.c_str(), "wb");
  if (!file) std::_Exit(13);
  std::fprintf(file, "P5\n%d %d\n255\n", renderer.getScreenWidth(), renderer.getScreenHeight());
  for (int y = 0; y < renderer.getScreenHeight(); ++y)
    for (int x = 0; x < renderer.getScreenWidth(); ++x) std::fputc(renderer.isPixelBlack(x, y) ? 0 : 255, file);
  std::fclose(file);
}
inline bool tick() {
  const char* directory = std::getenv("BOOKSYNC_EDITOR_CAPTURES");
  if (!directory) return false;
  mappedInputManager.simulatorClearInputFrame();
  static Fixture fixture;
  static unsigned frame = 0;
  const auto f = frame++;
  if (f == 0) {
    fixture.store.load();
    if (!fixture.store.project(0, fixture.page)) std::_Exit(14);
    showHomeScene(renderer, mappedInputManager, fixture);
  }
  {
    RenderLock lock;
    fixture.work();
  }
  auto key = [](MappedInputManager::Button button) { mappedInputManager.simulatorInjectRelease(button); };
  auto down = [&] { key(mappedInputManager.menuButton(MappedInputManager::Button::Down)); };
  if (fixture.page.textOnly) {
    // The unapproved Settings-list UI cannot be entered. Shared service parity
    // still uses real HAL files and restart, without a spatial editor on C3.
    if (f == 10) key(mappedInputManager.menuButton(MappedInputManager::Button::Right));
    if (f == 20) capture(directory, "x3-home-editor-disabled");
    if (f == 24 || f == 54 || f == 96) {
      LayoutEdit edit;
      edit.revision = fixture.page.revision;
      edit.kind = f == 24 ? LayoutEditKind::Add : f == 54 ? LayoutEditKind::Remove : LayoutEditKind::Reset;
      edit.widget = WidgetKind::Browse;
      if (fixture.editLayout(edit) != CommandStatus::Accepted) std::_Exit(15);
    }
  } else {
#if CROSSINK_APP_CAP_TOUCH
    if (mappedInputManager.hasTouch()) {
      const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
      const auto header = UITheme::getHeaderRect(renderer, UITheme::getInstance().getMetrics().headerHeight, safe);
      auto tap = [&](int x, int y) {
        mappedInputManager.simulatorInjectTouchDown(x, y);
        mappedInputManager.simulatorInjectTouchRelease(x, y);
      };
      auto toolbar = [&](unsigned i) {
        tap(header.x + 18 + i * (header.width - 24) / 3, header.y + renderer.getLineHeight(BITTER_14_FONT_ID) + 18);
      };
      auto row = [&](unsigned i) {
        tap(safe.x + 50, header.y + header.height + renderer.getLineHeight(UI_10_FONT_ID) * 3 + 40 +
                             i * (renderer.getLineHeight(UI_10_FONT_ID) + 26));
      };
      if (f == 10) tap(header.x + header.width - 40, header.y + 10);
      if (f == 30) toolbar(0);
      if (f == 38) row(2);
      if (f == 54) tap(safe.x + 40, header.y + header.height + 50);
      if (f == 60) toolbar(0);
      if (f == 84 || f == 104) toolbar(1);
      if (f == 92) row(0);
      if (f == 108) row(1);
    } else
#endif
    {
      if (f == 10) key(mappedInputManager.menuButton(MappedInputManager::Button::Right));
      if (f == 24 || f == 26 || f == 28 || f == 34 || f == 36 || f == 76 || f == 78 || f == 80 || f == 82 || f == 96 ||
          f == 98 || f == 100 || f == 102 || f == 106)
        down();
      if (f == 30 || f == 38 || f == 54 || f == 60 || f == 84 || f == 92 || f == 104 || f == 108)
        key(MappedInputManager::Button::Confirm);
    }
    if (f == 20) capture(directory, "edit-grid");
    if (f == 32) capture(directory, "add-picker");
    if (f == 90) capture(directory, "reset-keep-focused");
    if (f == 94 && fixture.page.revision != 3) std::_Exit(16);
    if (f == 124) key(MappedInputManager::Button::Back);
  }
  if (f == 50 || f == 72 || f == 120) {
    const unsigned revision = f == 50 ? 2 : f == 72 ? 3 : 4;
    const unsigned count = f == 50 ? 4 : 3;
    if (fixture.page.revision != revision || fixture.page.count != count ||
        fixture.reply.status != LayoutEditStatus::Saved) {
      LOG_ERR("BSE", "Editor scenario frame %u revision=%u count=%u status=%u", f, fixture.page.revision,
              fixture.page.count, unsigned(fixture.reply.status));
      std::_Exit(17);
    }
    if (!fixture.page.textOnly) capture(directory, f == 50 ? "added" : f == 72 ? "removed" : "reset");
  }
  if (f == 130) {
    fixture.store.load();
    if (!fixture.store.project(0, fixture.page) || fixture.page.revision != 4 || fixture.writes != 3) std::_Exit(18);
    LayoutEdit stale;
    stale.kind = LayoutEditKind::Remove;
    stale.revision = 1;
    if (fixture.store.edit(fixture.worker, stale) != LayoutEditStatus::Stale) std::_Exit(19);
    LOG_INF("BSE",
            "BookSync F35a PASS: add/remove/reset/restart/stale/keep; class=%s editor=%u payload-allocations=0; X3 UI "
            "NEEDS APPROVAL",
            Layout::deviceClass(), unsigned(sizeof(LayoutEditor)));
    std::fflush(nullptr);
    std::_Exit(0);
  }
  return true;
}
}  // namespace editorScenario
