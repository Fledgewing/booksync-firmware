// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "activities/Activity.h"
#include "booksync/system/Bootstrap.h"
#include "components/UITheme.h"
#include "fontIds.h"
extern GfxRenderer renderer;
extern MappedInputManager mappedInputManager;
namespace upgradeNoticeScenario {
using namespace booksync;
class Fixture final : public Library {
 public:
  unsigned syncs = 0, dismissals = 0;
  mutable unsigned reads = 0;
  Capabilities capabilities() const override { return {}; }
  Snapshot snapshot() const override {
    ++reads;
    Snapshot value;
    value.upgradeNoticePending = true;  // A pending checkpoint must not reopen this lifetime.
    return value;
  }
  AccountView accountView() const override {
    ++reads;
    AccountView value;
    value.serviceLabel = {"BookOrbit"};
    return value;
  }
  CommandStatus submit(Intent intent) override {
    if (intent == Intent::Sync) {
      ++syncs;
      return CommandStatus::Accepted;
    }
    if (intent == Intent::DismissUpgradeNotice) {
      ++dismissals;
      return CommandStatus::Accepted;
    }
    return CommandStatus::Unavailable;
  }
};
inline bool tick() {
  const char* directory = std::getenv("BOOKSYNC_NOTICE_CAPTURES");
  if (!directory) return false;
  const char* mode = std::getenv("BOOKSYNC_NOTICE_CHOICE");
  static Fixture fixture;
  static unsigned frame = 0;
  const auto f = frame++;
  mappedInputManager.simulatorClearInputFrame();
  if (f == 0) showHomeScene(renderer, mappedInputManager, fixture);
  if (f == 20) {
    const auto before = fixture.reads;
    if (activityManager.requestUpdateAndWait() != RequestUpdateResult::Rendered) std::_Exit(31);
    RenderLock lock;
    if (fixture.reads != before || fixture.syncs || fixture.dismissals) std::_Exit(32);
    std::filesystem::create_directories(directory);
    auto path = std::filesystem::path(directory) / "upgrade-notice.pgm";
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) std::_Exit(33);
    std::fprintf(file, "P5\n%d %d\n255\n", renderer.getScreenWidth(), renderer.getScreenHeight());
    for (int y = 0; y < renderer.getScreenHeight(); ++y)
      for (int x = 0; x < renderer.getScreenWidth(); ++x) std::fputc(renderer.isPixelBlack(x, y) ? 0 : 255, file);
    std::fclose(file);
  }
  const bool later = mode && !std::strcmp(mode, "later");
  const bool back = mode && !std::strcmp(mode, "back");
  if (f == 26 && later)
    mappedInputManager.simulatorInjectRelease(mappedInputManager.menuButton(MappedInputManager::Button::Down));
  if (f == 30) {
#if CROSSINK_APP_CAP_TOUCH
    if (mappedInputManager.hasTouch()) {
      const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
      const int height = renderer.getLineHeight(UI_10_FONT_ID) + 22;
      const int x = safe.x + 50;
      const int y = back ? safe.y + 15 : safe.y + safe.height - 42 - (later ? 1 : 2) * (height + 8) + 10;
      mappedInputManager.simulatorInjectTouchDown(x, y);
      mappedInputManager.simulatorInjectTouchRelease(x, y);
    } else
#endif
      mappedInputManager.simulatorInjectRelease(back ? MappedInputManager::Button::Back
                                                     : MappedInputManager::Button::Confirm);
  }
  if (f == 60) {
    if (fixture.dismissals != 1 || fixture.syncs != unsigned(!later && !back)) std::_Exit(34);
    LOG_INF("BSN", "BookSync B2 PASS: static paint, once-only, choice=%s, sync=%u dismiss=%u", mode, fixture.syncs,
            fixture.dismissals);
    std::fflush(nullptr);
    std::_Exit(0);
  }
  return true;
}
}  // namespace upgradeNoticeScenario
