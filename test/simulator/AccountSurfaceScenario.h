// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "activities/Activity.h"
#include "booksync/system/AccountScreen.h"
#include "booksync/system/Bootstrap.h"
#include "components/UITheme.h"
#include "fontIds.h"
extern GfxRenderer renderer;
extern MappedInputManager mappedInputManager;
namespace accountSurfaceScenario {
using namespace booksync;
class Fixture final : public Library {
 public:
  AccountView value{};
  mutable unsigned reads = 0;
  unsigned writes = 0;
  ServiceSelection selection{};
  Fixture() {
    value.service = 7;
    value.available = true;
    value.ticket = {8, 5};
    std::memcpy(value.serviceLabel.data(), "Library service", 16);
  }
  Capabilities capabilities() const override { return {}; }
  Snapshot snapshot() const override { return {}; }
  CommandStatus submit(Intent) override { return CommandStatus::Unavailable; }
  AccountView accountView() const override {
    ++reads;
    return value;
  }
  CommandStatus selectService(ServiceSelection request) override {
    if (request.ticket.owner != value.ticket.owner || request.ticket.epoch != value.ticket.epoch)
      return CommandStatus::Unavailable;
    ++writes;
    selection = request;
    return CommandStatus::Accepted;
  }
};
inline void capture(const char* directory, const char* name, const Fixture& fixture) {
  const auto reads = fixture.reads, writes = fixture.writes;
  if (activityManager.requestUpdateAndWait() != RequestUpdateResult::Rendered) std::_Exit(21);
  RenderLock lock;
  if (reads != fixture.reads || writes != fixture.writes) std::_Exit(22);
  std::filesystem::create_directories(directory);
  auto path = std::filesystem::path(directory) / (std::string(name) + ".pgm");
  FILE* file = std::fopen(path.c_str(), "wb");
  if (!file) std::_Exit(23);
  std::fprintf(file, "P5\n%d %d\n255\n", renderer.getScreenWidth(), renderer.getScreenHeight());
  for (int y = 0; y < renderer.getScreenHeight(); ++y)
    for (int x = 0; x < renderer.getScreenWidth(); ++x) std::fputc(renderer.isPixelBlack(x, y) ? 0 : 255, file);
  std::fclose(file);
}
inline bool tick() {
  const char* directory = std::getenv("BOOKSYNC_ACCOUNT_SURFACE");
  if (!directory) return false;
  mappedInputManager.simulatorClearInputFrame();
  static Fixture fixture;
  static unsigned frame = 0, closedPaints = 0, closedReads = 0;
  const auto f = frame++;
  auto key = [](MappedInputManager::Button b) { mappedInputManager.simulatorInjectRelease(b); };
  if (f == 0) showHomeScene(renderer, mappedInputManager, fixture);
  if (f == 8 || f == 70) showServiceChoice(renderer, mappedInputManager, fixture);
  if (f == 16) capture(directory, "service-choice", fixture);
  bool touchChoice = false;
#if CROSSINK_APP_CAP_TOUCH
  touchChoice = mappedInputManager.hasTouch();
  if (touchChoice && (f == 24 || f == 40)) {
    const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const auto header = UITheme::getHeaderRect(renderer, UITheme::getInstance().getMetrics().headerHeight, safe);
    const int height = renderer.getLineHeight(UI_10_FONT_ID) * 2 + 28;
    const int index = f == 24 ? 1 : 2;
    const int x = safe.x + 50, y = header.y + header.height + 110 + index * (height + 12) + 15;
    mappedInputManager.simulatorInjectTouchDown(x, y);
    mappedInputManager.simulatorInjectTouchRelease(x, y);
  }
#endif
  if (f == 20) key(mappedInputManager.menuButton(MappedInputManager::Button::Down));
  if (f == 24 && !touchChoice) key(MappedInputManager::Button::Confirm);
  if (f == 28) capture(directory, "saved-books-selected", fixture);
  if (f == 32) key(mappedInputManager.menuButton(MappedInputManager::Button::Down));
  if (f == 36) capture(directory, "continue-focused", fixture);
  if (f == 40 && !touchChoice) key(MappedInputManager::Button::Confirm);
  if (f == 48) {
    if (fixture.writes != 1 || fixture.selection.service != 0 || fixture.selection.ticket.epoch != 5) std::_Exit(24);
    closedPaints = serviceChoicePaints();
    closedReads = fixture.reads;
  }
  if (f == 52) {
    fixture.value.signedIn = true;
    fixture.value.ticket.epoch += 2;
  }
  if (f == 64 && (closedPaints != serviceChoicePaints() || closedReads != fixture.reads)) std::_Exit(25);
  if (f == 78) capture(directory, "reopened-current-epoch", fixture);
  if (f == 82) ++fixture.value.ticket.epoch;
  if (f == 90) {
    closedPaints = serviceChoicePaints();
    closedReads = fixture.reads;
  }
  if (f == 100) {
    if (closedPaints != serviceChoicePaints() || closedReads != fixture.reads || fixture.writes != 1) std::_Exit(26);
    LOG_INF("BSA", "BookSync F37a PASS: 4 captures, 2 closures, 0 paint work, bounded view=%u",
            unsigned(sizeof(AccountView)));
    std::_Exit(0);
  }
  return true;
}
}  // namespace accountSurfaceScenario
