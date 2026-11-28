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
  AccountFormView formValue{};
  bool formClosed = false;
  unsigned formSubmissions = 0, formChecks = 0;
  CommandStatus accountAction(AccountFormRequest request) override {
    ++writes;
    if (request.action == AccountAction::Begin) {
      formClosed = false;
      formValue.form++;
      formValue.account = value;
      formValue.origin = value.ticket;
      ++formValue.revision;
    } else if (request.action == AccountAction::Cancel)
      formClosed = true;
    else if (request.action == AccountAction::Manual) {
      formValue.scene = AccountScene::Manual;
      ++formValue.revision;
    } else if (request.action == AccountAction::Check) {
      formValue.scene = AccountScene::Wait;
      ++formChecks;
      ++formValue.revision;
    } else if (request.action == AccountAction::Phone) {
      formValue.scene = AccountScene::Phone;
      ++formValue.revision;
    }
    return CommandStatus::Accepted;
  }
  void readAccountForm(AccountFormView& out) const override {
    ++reads;
    out = formValue;
  }
  CommandStatus submitAccount(AccountFormRequest, AccountEntry& entry) override {
    ++writes;
    ++formSubmissions;
    if (entry.password[0] != '1') std::_Exit(33);
    entry.clear();
    if (entry.password[0]) std::_Exit(34);
    formValue.scene = AccountScene::Busy;
    ++formValue.revision;
    return CommandStatus::Accepted;
  }
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
inline bool formTick() {
  const char* directory = std::getenv("BOOKSYNC_ACCOUNT_FORMS");
  if (!directory) return false;
  mappedInputManager.simulatorClearInputFrame();
  static Fixture fixture;
  static unsigned frame = 0, closedPaints = 0, closedReads = 0;
  const unsigned f = frame++;
  auto key = [](MappedInputManager::Button b) { mappedInputManager.simulatorInjectRelease(b); };
  if (f == 0) {
    fixture.value.accountSupported = fixture.value.syncSupported = fixture.value.catalogSupported = true;
    showHomeScene(renderer, mappedInputManager, fixture);
  }
  if (f == 8) showServiceChoice(renderer, mappedInputManager, fixture);
  if (f == 16 || f == 20) key(mappedInputManager.menuButton(MappedInputManager::Button::Down));
  if (f == 24) key(MappedInputManager::Button::Confirm);
  static const char* names[] = {"account-out",    "account-in",       "account-limited", "account-manual",
                                "account-busy",   "account-phone",    "account-wait",    "account-expired",
                                "account-failed", "account-two-step", "account-old"};
  if (f >= 32 && f < 32 + 11 * 24) {
    const unsigned scene = (f - 32) / 24, step = (f - 32) % 24;
    if (step == 0) {
      fixture.formValue.scene = static_cast<AccountScene>(scene);
      fixture.formValue.account.accountSupported = fixture.formValue.account.syncSupported = scene != 2;
      fixture.formValue.account.catalogSupported = true;
      fixture.formValue.account.signedIn = scene == 1;
      fixture.formValue.account.syncReady = scene == 1;
      fixture.formValue.account.catalogReady = scene == 1 || scene == 2;
      fixture.formValue.link.fill(0);
      if (scene == 5 || scene == 6) {
        // Synthetic public approval data; no private address, identity or token.
        fixture.formValue.link.fill('a');
        std::memcpy(fixture.formValue.link.data(), "https://", 8);
        fixture.formValue.link[16] = '/';
        fixture.formValue.link[32] = 0;
      }
      ++fixture.formValue.revision;
    }
    if (step == 8) capture(directory, names[scene], fixture);
  }
  bool touchForm = false;
#if CROSSINK_APP_CAP_TOUCH
  touchForm = mappedInputManager.hasTouch();
  if (touchForm && (f == 114 || f == 118 || f == 126 || f == 164)) {
    const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const auto header = UITheme::getHeaderRect(renderer, UITheme::getInstance().getMetrics().headerHeight, safe);
    const int height = renderer.getLineHeight(UI_10_FONT_ID) + 24;
    const int keyboardY = safe.y + safe.height - 48 - (renderer.getLineHeight(UI_10_FONT_ID) * 5 + 130);
    int x = safe.x + 24, y = keyboardY + 16;
    if (f == 114) y = header.y + header.height + 12 + 132 + 2 * (height + 6) + 12;
    if (f == 126) {
      x = safe.x + safe.width * 3 / 4;
      y = keyboardY - height - 4;
    }
    if (f == 164) y = safe.y + safe.height - 56 - 3 * (height + 8) + 16;
    mappedInputManager.simulatorInjectTouchDown(x, y);
    mappedInputManager.simulatorInjectTouchRelease(x, y);
  }
#endif
  if (!touchForm) {
    if ((f >= 114 && f <= 117) || f == 119) key(mappedInputManager.menuButton(MappedInputManager::Button::Down));
    if (f == 118 || f == 120 || f == 126 || f == 164) key(MappedInputManager::Button::Confirm);
    if (f >= 122 && f <= 125) key(mappedInputManager.menuButton(MappedInputManager::Button::Up));
  }
  if (f == 121) capture(directory, "account-manual-entered", fixture);
  if (f == 132 && fixture.formSubmissions != 1) std::_Exit(35);
  if (f == 172 && fixture.formChecks != 1) std::_Exit(36);
  if (f == 168) {
    fixture.formValue.scene = AccountScene::Phone;
    fixture.formValue.link.fill('W');
    std::memcpy(fixture.formValue.link.data(), "https://", 8);
    for (unsigned i = 8; i < 16; ++i) fixture.formValue.link[i] = 'a';
    fixture.formValue.link[16] = '/';
    fixture.formValue.link[254] = 'Z';
    fixture.formValue.link[255] = 0;
    ++fixture.formValue.revision;
  }
  if (f == 172) capture(directory, "account-long-link", fixture);
  if (f == 174) {
    fixture.formValue.scene = AccountScene::Wait;
    ++fixture.formValue.revision;
  }
  if (f == 175) capture(directory, "account-long-wait", fixture);
  if (f == 304) key(MappedInputManager::Button::Back);
  if (f == 312) {
    closedPaints = accountFormPaints();
    closedReads = fixture.reads;
  }
  if (f == 316) {
    fixture.value.ticket.epoch += 2;
    fixture.formValue.account = fixture.value;
    fixture.formValue.scene = AccountScene::In;
    ++fixture.formValue.revision;
  }
  if (f == 328) {
    if (closedPaints != accountFormPaints() || closedReads != fixture.reads) std::_Exit(31);
    showAccountForm(renderer, mappedInputManager, fixture);
  }
  if (f == 336) ++fixture.value.ticket.epoch;
  if (f == 344) {
    closedPaints = accountFormPaints();
    closedReads = fixture.reads;
  }
  if (f == 356) {
    if (closedPaints != accountFormPaints() || closedReads != fixture.reads) std::_Exit(32);
    LOG_INF("BSA", "BookSync F37b PASS: 14 captures, picker navigation, 2 closures, 0 paint work");
    std::_Exit(0);
  }
  return true;
}
inline bool tick() {
  if (formTick()) return true;
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
    showServiceChoice(renderer, mappedInputManager, fixture);
  }
  if (f == 108) {
    capture(directory, "back-ready", fixture);
    if (closedPaints >= serviceChoicePaints() || closedReads >= fixture.reads) std::_Exit(29);
  }
  if (f == 112) {
#if CROSSINK_APP_CAP_TOUCH
    if (touchChoice) {
      const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
      mappedInputManager.simulatorInjectTouchDown(safe.x + 40, safe.y + 20);
      mappedInputManager.simulatorInjectTouchRelease(safe.x + 40, safe.y + 20);
    } else
#endif
      key(MappedInputManager::Button::Back);
  }
  if (f == 120) {
    closedPaints = serviceChoicePaints();
    closedReads = fixture.reads;
  }
  if (f == 128 && (closedPaints != serviceChoicePaints() || closedReads != fixture.reads || fixture.writes != 1))
    std::_Exit(27);
  if (f == 132) ++fixture.value.ticket.epoch;
  if (f == 144) {
    if (closedPaints != serviceChoicePaints() || closedReads != fixture.reads || fixture.writes != 1) std::_Exit(28);
    LOG_INF("BSA", "BookSync F37a PASS: 5 captures, 3 closures, 0 paint work, bounded view=%u",
            unsigned(sizeof(AccountView)));
    std::_Exit(0);
  }
  return true;
}
}  // namespace accountSurfaceScenario
