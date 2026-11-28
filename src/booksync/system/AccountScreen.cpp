// SPDX-License-Identifier: GPL-3.0-or-later
#include "AccountScreen.h"

#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "booksync/ui/account/ServiceChoice.h"
#include "components/UITheme.h"
#include "fontIds.h"
namespace booksync {
namespace {
#ifdef SIMULATOR
uint32_t paints = 0;
#endif
class ServiceChoiceActivity final : public Activity {
  ServiceChoice choice;
  Rect row(uint8_t index) const {
    const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const auto header = UITheme::getHeaderRect(renderer, UITheme::getInstance().getMetrics().headerHeight, safe);
    const int height = renderer.getLineHeight(UI_10_FONT_ID) * 2 + 28;
    return {safe.x + 20, header.y + header.height + 110 + index * (height + 12), safe.width - 40, height};
  }
  const char* label(uint8_t index) const {
    if (index == 0) return choice.view().serviceLabel[0] ? choice.view().serviceLabel.data() : tr(STR_HOME_UNAVAILABLE);
    if (index == 1) return tr(STR_ACCOUNT_SAVED_BOOKS);
    if (index == 2) return tr(STR_CONTINUE);
    return tr(STR_ACCOUNT_LATER);
  }

 public:
  ServiceChoiceActivity(GfxRenderer& renderer, MappedInputManager& input, Library& library)
      : Activity("ServiceChoice", renderer, input), choice(library) {}
  void onEnter() override {
    Activity::onEnter();
    choice.enter();
    requestUpdate();
  }
  void onExit() override {
    choice.retireChoice();
    Activity::onExit();
  }
  void loop() override {
    RenderLock lock;
    if (choice.pollChoice()) {
      activityManager.popActivity();
      return;
    }
    bool changed = false;
    int x = 0, y = 0;
    const bool tapped = mappedInput.wasScreenTapped(x, y);
    const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        (tapped && x >= safe.x && x < safe.x + 100 && y >= safe.y && y < safe.y + 70)) {
      choice.retireChoice();
    } else if (mappedInput.wasReleased(mappedInput.menuButton(MappedInputManager::Button::Down))) {
      choice.stepChoice(1);
      changed = true;
    } else if (mappedInput.wasReleased(mappedInput.menuButton(MappedInputManager::Button::Up))) {
      choice.stepChoice(-1);
      changed = true;
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      choice.activateChoice();
      changed = true;
    } else if (tapped) {
      for (uint8_t i = 0; i < 4; ++i) {
        const auto r = row(i);
        if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height) {
          choice.chooseChoice(i);
          changed = true;
          break;
        }
      }
    }
    if (!choice.live())
      activityManager.popActivity();
    else if (changed)
      requestUpdate();
  }
  void render(RenderLock&&) override {
#ifdef SIMULATOR
    ++paints;
#endif
    // All values were captured outside paint. No Library reads or dispatch here.
    renderer.clearScreen();
    const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    GUI.drawHeader(renderer, UITheme::getHeaderRect(renderer, UITheme::getInstance().getMetrics().headerHeight, safe),
                   tr(STR_ACCOUNT_CHOOSE_SERVICE));
    renderer.drawText(UI_10_FONT_ID, safe.x + 20, row(0).y - 60, tr(STR_ACCOUNT_SETUP_LATER));
    for (uint8_t i = 0; i < 4; ++i) {
      const auto r = row(i);
      const bool selected = i < 2 && i == choice.selected();
      const bool focused = i == choice.focus();
      renderer.drawRect(r.x, r.y, r.width, r.height, focused ? 3 : 1, true);
      if (selected) renderer.drawRect(r.x + 5, r.y + 5, r.width - 10, r.height - 10);
      renderer.beginTextClip(r.x + 12, r.y + 8, r.width - 24, r.height - 16);
      renderer.drawText(UI_10_FONT_ID, r.x + 12, r.y + 10, label(i));
      if (i < 2)
        renderer.drawText(UI_10_FONT_ID, r.x + 12, r.y + renderer.getLineHeight(UI_10_FONT_ID) + 14,
                          i == 1                    ? tr(STR_ACCOUNT_NO_SIGNIN)
                          : choice.view().available ? tr(STR_ACCOUNT_AVAILABLE)
                                                    : tr(STR_HOME_UNAVAILABLE));
      renderer.endTextClip();
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_HOME_PREV), tr(STR_HOME_NEXT));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
};
}  // namespace
void showServiceChoice(GfxRenderer& renderer, MappedInputManager& input, Library& library) {
  // ActivityManager owns one fallible small screen allocation; no framebuffer,
  // credential buffer, task or network allocation is added.
  auto screen = makeUniqueNoThrow<ServiceChoiceActivity>(renderer, input, library);
  if (!screen) {
    LOG_ERR("BSA", "Cannot allocate service choice");
    return;
  }
  activityManager.pushActivity(std::move(screen));
}
#ifdef SIMULATOR
uint32_t serviceChoicePaints() {
  RenderLock lock;
  return paints;
}
#endif
}  // namespace booksync

// Keep the approved dormant presentation in device reserve measurements before
// the separate first-run/settings navigation slice calls the explicit seam.
extern "C" void booksync_account_surface_link(GfxRenderer& renderer, MappedInputManager& input,
                                              booksync::Library& library) {
  booksync::showServiceChoice(renderer, input, library);
}
