// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "activities/Activity.h"
#include "booksync/services/LayoutGeometry.h"
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
  LayoutEditStatus forced = LayoutEditStatus::Idle;
  mutable unsigned paints = 0;
  unsigned writes = 0, batches = 0, opens = 0;
  Capabilities capabilities() const override { return {}; }
  Snapshot snapshot() const override { return {}; }
  HomePage homePage() const override { return page; }
  bool requestHomePage(uint8_t index) override { return store.project(index, page); }
  bool requestBatch(WindowBatchRequest) override {
    ++batches;
    return true;
  }
  CommandStatus submit(Intent intent) override {
    if (intent == Intent::OpenLastBook) ++opens;
    return CommandStatus::Unavailable;
  }
  CommandStatus editLayout(LayoutEdit edit) override {
    if (queued) return CommandStatus::Unavailable;
    request = edit;
    queued = true;
    reply.status = LayoutEditStatus::Pending;
    return CommandStatus::Accepted;
  }
  LayoutPreview previewLayout(HomePage draft, uint8_t index, LayoutStep movement) const override {
    if (draft.revision != page.revision) return {draft, LayoutEditStatus::Stale};
    return previewGeometry(draft, index, movement);
  }
  LayoutEditResult layoutEditResult() const override { return reply; }
  void work() {
    if (!queued) return;
    queued = false;
    ++writes;
    reply.status = forced == LayoutEditStatus::Idle ? store.edit(worker, request) : forced;
    forced = LayoutEditStatus::Idle;
    const auto index = reply.status != LayoutEditStatus::Saved      ? page.page
                       : request.kind == LayoutEditKind::CreatePage ? request.page + 1
                       : request.kind == LayoutEditKind::MovePage   ? request.index
                       : request.kind == LayoutEditKind::Reset      ? 0
                                                                    : request.page;
    if (!store.project(index, page)) std::_Exit(11);
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
inline bool geometryTick() {
  const char* directory = std::getenv("BOOKSYNC_GEOMETRY_CAPTURES");
  if (!directory) return false;
  mappedInputManager.simulatorClearInputFrame();
  static Fixture fixture;
  static unsigned frame = 0;
  const auto f = frame++;
  if (f == 0) {
    fixture.store.load();
    if (!fixture.store.project(0, fixture.page)) std::_Exit(30);
    showHomeScene(renderer, mappedInputManager, fixture, fixture.page.textOnly);
  }
  {
    RenderLock lock;
    fixture.work();
  }
  auto key = [](MappedInputManager::Button button) { mappedInputManager.simulatorInjectRelease(button); };
  auto down = [&] { key(mappedInputManager.menuButton(MappedInputManager::Button::Down)); };
  const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const auto header = UITheme::getHeaderRect(renderer, UITheme::getInstance().getMetrics().headerHeight, safe);
#if CROSSINK_APP_CAP_TOUCH
  auto tap = [&](int x, int y) {
    mappedInputManager.simulatorInjectTouchDown(x, y);
    mappedInputManager.simulatorInjectTouchRelease(x, y);
  };
  auto moveButton = [&](unsigned i) { tap(safe.x + 12 + i * ((safe.width - 16) / 7), safe.y + safe.height - 28); };
#endif
  if (fixture.page.textOnly) {
    if (f == 10) key(mappedInputManager.menuButton(MappedInputManager::Button::Right));
    if (f == 8) key(MappedInputManager::Button::Confirm);
    if (f == 20) capture(directory, "x3-no-spatial-controls");
    if (f == 22) key(MappedInputManager::Button::Back);
    if (f == 24) {
      auto draft = fixture.page;
      draft.textOnly = false;  // Service parity only; no X3 presentation.
      auto preview = previewGeometry(draft, 0, LayoutStep::Down);
      if (preview.status != LayoutEditStatus::Saved) std::_Exit(31);
      LayoutEdit edit;
      edit.kind = LayoutEditKind::Geometry;
      edit.revision = 1;
      edit.count = draft.count;
      for (uint8_t i = 0; i < edit.count; ++i) {
        const auto& t = preview.page.widgets[i];
        edit.geometry[i] = {t.x, t.y, t.w, t.h};
      }
      if (fixture.store.edit(fixture.worker, edit) != LayoutEditStatus::Invalid) std::_Exit(32);
    }
  } else {
#if CROSSINK_APP_CAP_TOUCH
    if (mappedInputManager.hasTouch()) {
      if (f == 10) tap(header.x + header.width - 40, header.y + 10);
      if (f == 24 || f == 48 || f == 72) {
        const auto& tile = fixture.page.widgets[0];
        const int top = header.y + header.height + 16;
        const int height = safe.y + safe.height - top - 56;
        tap(safe.x + 30, top + height * tile.y / fixture.page.rows + 24);
      }
      if (f == 30 || f == 52 || f == 77) moveButton(2);
      if (f == 36) moveButton(4);
      if (f == 56)
        tap(header.x + 18 + (header.width - 24) / 2, header.y + renderer.getLineHeight(BITTER_14_FONT_ID) + 18);
    } else
#endif
    {
      if (f == 10 || f == 36) key(mappedInputManager.menuButton(MappedInputManager::Button::Right));
      if (f == 24 || f == 48 || f == 56 || f == 72) key(MappedInputManager::Button::Confirm);
      if (f == 30 || f == 52 || f == 77) down();
    }
    if (f == 20) capture(directory, "edit-grid");
    if (f == 28) capture(directory, "carrying");
    if (f == 34) capture(directory, "band-moved");
    if (f == 38) capture(directory, "resized");
    if (f == 40) key(MappedInputManager::Button::Back);
    if (f == 44 && (fixture.writes || fixture.page.revision != 1 || fixture.page.widgets[0].y)) std::_Exit(33);
    if (f == 64 && (fixture.writes != 1 || fixture.page.revision != 2 || fixture.page.widgets[0].y != 2 ||
                    fixture.page.widgets[1].y != 0))
      std::_Exit(34);
    if (f == 68) capture(directory, "saved");
    if (f == 76) ++fixture.page.revision;
    if (f == 80) capture(directory, "stale");
    if (f == 84 || f == 88) key(MappedInputManager::Button::Back);
  }
  if (f == 96) {
    fixture.store.load();
    if (!fixture.store.project(0, fixture.page) || fixture.page.revision != (fixture.page.textOnly ? 1u : 2u))
      std::_Exit(35);
    LOG_INF("BSE",
            "BookSync F35b PASS: band/resize/cancel/drop/focus/stale/restart; workspace=%u; payload-allocations=0; X3 "
            "spatial editor disabled",
            unsigned(sizeof(LayoutEditor)));
    std::fflush(nullptr);
    std::_Exit(0);
  }
  return true;
}
inline bool settingsTick(const char* directory) {
  static unsigned frame = 0;
  const auto f = frame++;
  mappedInputManager.simulatorClearInputFrame();
  if (f == 0) showHome(renderer, mappedInputManager, HalDisplay::FAST_REFRESH);
  if (f == 8)
    mappedInputManager.simulatorInjectRelease(mappedInputManager.menuButton(MappedInputManager::Button::Right));
  if (f == 12) {
    if (!activityManager.isCurrentActivityNamed("Settings")) std::_Exit(26);
    capture(directory, "x3-settings-menu");
  }
  if (f == 14) mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Confirm);
  if (f == 30) {
    if (!activityManager.isCurrentActivityNamed("Home")) std::_Exit(24);
    capture(directory, "x3-settings-entry");
  }
  if (f == 32) mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Back);
  if (f == 45) {
    if (!activityManager.isCurrentActivityNamed("Settings")) std::_Exit(25);
  }
  if (f == 47) mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Back);
  if (f == 60) {
    if (!activityManager.isCurrentActivityNamed("Home")) std::_Exit(27);
    LOG_INF("BSE", "BookSync X3 Settings entry/back PASS");
    std::fflush(nullptr);
    std::_Exit(0);
  }
  return true;
}
inline bool pagesTick() {
  const char* directory = std::getenv("BOOKSYNC_PAGES_CAPTURES");
  if (!directory) return false;
  mappedInputManager.simulatorClearInputFrame();
  static Fixture fixture;
  static unsigned frame = 0;
  const auto f = frame++;
  if (f == 0) {
    fixture.store.load();
    if (!fixture.store.project(0, fixture.page)) std::_Exit(40);
    showHomeScene(renderer, mappedInputManager, fixture);
  }
  {
    RenderLock lock;
    fixture.work();
  }
  auto key = [](MappedInputManager::Button b) { mappedInputManager.simulatorInjectRelease(b); };
  auto down = [&] { key(mappedInputManager.menuButton(MappedInputManager::Button::Down)); };
  const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
#if CROSSINK_APP_CAP_TOUCH
  const auto header = UITheme::getHeaderRect(renderer, UITheme::getInstance().getMetrics().headerHeight, safe);
  auto tap = [&](int x, int y) {
    mappedInputManager.simulatorInjectTouchDown(x, y);
    mappedInputManager.simulatorInjectTouchRelease(x, y);
  };
  auto footer = [&](int i) { tap(safe.x + 12 + i * ((safe.width - 16) / 6), safe.y + safe.height - 28); };
#endif
  if (fixture.page.textOnly) {
    if (f == 10) key(mappedInputManager.menuButton(MappedInputManager::Button::Right));
    if (f == 20) capture(directory, "x3-pages-disabled");
    if (f == 24) {
      LayoutEdit edit;
      edit.revision = 1;
      edit.kind = LayoutEditKind::CreatePage;
      if (fixture.store.edit(fixture.worker, edit) != LayoutEditStatus::Invalid) std::_Exit(41);
    }
  } else {
#if CROSSINK_APP_CAP_TOUCH
    if (mappedInputManager.hasTouch()) {
      if (f == 10) tap(header.x + header.width - 40, header.y + 10);
      if (f == 22) footer(3);
      if (f == 24) capture(directory, "naming-default-home");
      if (f == 26) key(MappedInputManager::Button::Back);
      if (f == 40) footer(2);
      if (f == 50 || f == 70) footer(3);
      // Cancel the first name entry. The second saves a key from the SDK keyboard.
      if (f == 56) key(MappedInputManager::Button::Back);
      if (f == 76) tap(safe.x + 24, safe.y + safe.height - 250 + 24);
      if (f == 86) {
        {
          RenderLock lock;
          renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
        }
        capture(directory, "naming-landscape");
      }
      if (f == 88) {
        {
          RenderLock lock;
          renderer.setOrientation(GfxRenderer::Orientation::Portrait);
        }
        if (activityManager.requestUpdateAndWait() != RequestUpdateResult::Rendered) std::_Exit(47);
      }
      if (f == 104) tap(safe.x + safe.width - 32, safe.y + safe.height - 24);
      if (f == 126) footer(4);
    } else
#endif
    {
      if (f == 10) key(mappedInputManager.menuButton(MappedInputManager::Button::Right));
      if ((f >= 24 && f <= 38 && f % 2 == 0) || (f >= 44 && f <= 49) ||
          ((f >= 60 && f <= 68 && f % 2 == 0) || f == 69) || (f >= 78 && f <= 100 && f % 2 == 0) ||
          (f >= 110 && f <= 122 && f % 2 == 0))
        down();
      if (f == 40 || f == 50 || f == 70 || f == 76 || f == 104 || f == 126) key(MappedInputManager::Button::Confirm);
      if (f == 56) key(MappedInputManager::Button::Back);
    }
    if (f == 20) capture(directory, "page-controls");
    if (f == 42) {
      if (fixture.page.page != 1 || fixture.page.pages != 2 || fixture.page.count || fixture.writes != 1)
        std::_Exit(42);
      capture(directory, "created");
    }
    if (f == 54) capture(directory, "naming-cancel");
    if (f == 58 && fixture.writes != 1) std::_Exit(43);
    if (f == 74) capture(directory, "naming");
    if (f == 108) {
      if (fixture.page.revision != 3 || fixture.writes != 2 ||
          std::strcmp(fixture.page.title.data(), mappedInputManager.hasTouch() ? "Page 2q" : "Home"))
        std::_Exit(44);
      capture(directory, "named");
    }
    if (f == 130) {
      if (fixture.page.page != 0 || fixture.page.count || fixture.page.revision != 4 || fixture.writes != 3)
        std::_Exit(45);
      capture(directory, "reordered");
    }
    if (f == 132) key(MappedInputManager::Button::Back);
    if (f == 134) key(MappedInputManager::Button::Confirm);
    if (f == 138 || f == 144) {
#if CROSSINK_APP_CAP_TOUCH
      if (mappedInputManager.hasTouch())
        tap(safe.x + (f == 138 ? safe.width - 32 : 32), safe.y + safe.height - 28);
      else
#endif
        key(mappedInputManager.menuButton(f == 138 ? MappedInputManager::Button::Down
                                                   : MappedInputManager::Button::Up));
    }
    if (f == 142) {
      if (fixture.page.page != 1 || fixture.page.count != 3 || fixture.opens) std::_Exit(48);
      capture(directory, "empty-page-next");
    }
    if (f == 148) {
      if (fixture.page.page != 0 || fixture.page.count || fixture.opens) std::_Exit(49);
      capture(directory, "empty-page-return");
    }
    if (f == 150) {
#if CROSSINK_APP_CAP_TOUCH
      if (mappedInputManager.hasTouch())
        tap(header.x + header.width - 40, header.y + 10);
      else
#endif
        key(mappedInputManager.menuButton(MappedInputManager::Button::Right));
    }
    if (f == 154) ++fixture.page.revision;
    if (f == 156) capture(directory, "stale");
    if (f == 160 || f == 162) key(MappedInputManager::Button::Back);
  }
  if (f == 168) {
    fixture.store.load();
    if (!fixture.store.project(0, fixture.page) || fixture.page.revision != (fixture.page.textOnly ? 1u : 4u))
      std::_Exit(46);
    LOG_INF("BSE",
            "BookSync F35c PASS: create/name/reorder/empty-page/cancel/stale/restart; workspace=%u; "
            "payload-allocations=0; X3 "
            "pages disabled",
            unsigned(sizeof(LayoutEditor)));
    std::fflush(nullptr);
    std::_Exit(0);
  }
  return true;
}
inline bool tick() {
  if (pagesTick()) return true;
  if (geometryTick()) return true;
  const char* directory = std::getenv("BOOKSYNC_EDITOR_CAPTURES");
  if (!directory) return false;
  if (std::getenv("BOOKSYNC_EDITOR_SETTINGS")) return settingsTick(directory);
  mappedInputManager.simulatorClearInputFrame();
  static Fixture fixture;
  static unsigned frame = 0, idlePaints = 0;
  const auto f = frame++;
  if (f == 0) {
    fixture.store.load();
    if (!fixture.store.project(0, fixture.page)) std::_Exit(14);
    showHomeScene(renderer, mappedInputManager, fixture, fixture.page.textOnly);
  }
  {
    RenderLock lock;
    fixture.work();
  }
  auto key = [](MappedInputManager::Button button) { mappedInputManager.simulatorInjectRelease(button); };
  auto down = [&] { key(mappedInputManager.menuButton(MappedInputManager::Button::Down)); };
  if (fixture.page.textOnly) {
    if (f == 20) capture(directory, "x3-list");
    if (f == 24 || f == 26 || f == 28 || f == 34 || f == 36 || f == 62 || f == 76 || f == 78 || f == 80 || f == 82 ||
        f == 96 || f == 98 || f == 100 || f == 102 || f == 106 || f == 134 || f == 136 || f == 138 || f == 154 ||
        f == 156 || f == 158)
      down();
    if (f == 30 || f == 38 || f == 54 || f == 58 || f == 60 || f == 64 || f == 84 || f == 92 || f == 104 || f == 108 ||
        f == 140 || f == 144 || f == 152 || f == 160 || f == 164)
      key(MappedInputManager::Button::Confirm);
    if (f == 32) capture(directory, "x3-add");
    if (f == 56) capture(directory, "x3-remove-keep");
    if (f == 90) capture(directory, "x3-reset-keep");
    if (f == 94 && fixture.page.revision != 3) std::_Exit(16);
    if (f == 142) fixture.forced = LayoutEditStatus::NoSpace;
    if (f == 162) fixture.forced = LayoutEditStatus::StorageError;
    if (f == 150) capture(directory, "x3-no-space");
    if (f == 170) capture(directory, "x3-save-failed");
    if (f == 172 || f == 180) key(MappedInputManager::Button::Back);
    if (f == 174) capture(directory, "x3-recovered-list");
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
      if (f == 30 || f == 34) toolbar(0);
      if (f == 33) tap(header.x + header.width - 40, header.y + 10);
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
      if (f == 54) key(mappedInputManager.menuButton(MappedInputManager::Button::Right));
      if (f == 30 || f == 38 || f == 60 || f == 84 || f == 92 || f == 104 || f == 108)
        key(MappedInputManager::Button::Confirm);
    }
    if (f == 14) {
      if (activityManager.requestUpdateAndWait() != RequestUpdateResult::Rendered) std::_Exit(22);
      RenderLock lock;
      idlePaints = homeScenePaints();
    }
    if (f == 18) {
      RenderLock lock;
      if (homeScenePaints() != idlePaints) std::_Exit(23);
    }
    if (f == 20) capture(directory, "edit-grid");
    if (f == 32) capture(directory, "add-picker");
    if (f == 35 && (fixture.page.revision != 1 || fixture.writes != 0)) std::_Exit(21);
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
    capture(directory, f == 50 ? "added" : f == 72 ? "removed" : "reset");
  }
  if (f == (fixture.page.textOnly ? 190u : 130u)) {
    fixture.store.load();
    if (!fixture.store.project(0, fixture.page) || fixture.page.revision != 4 ||
        fixture.writes != (fixture.page.textOnly ? 5u : 3u) || fixture.batches != 4)
      std::_Exit(18);
    LayoutEdit stale;
    stale.kind = LayoutEditKind::Remove;
    stale.revision = 1;
    if (fixture.store.edit(fixture.worker, stale) != LayoutEditStatus::Stale) std::_Exit(19);
    LOG_INF("BSE",
            "BookSync F35a PASS: add/remove/reset/restart/stale/keep/idle/one-batch; class=%s editor=%u "
            "payload-allocations=0; approved X3 Settings list",
            Layout::deviceClass(), unsigned(sizeof(LayoutEditor)));
    std::fflush(nullptr);
    std::_Exit(0);
  }
  return true;
}
}  // namespace editorScenario
