// SPDX-License-Identifier: GPL-3.0-or-later
#include "AccountScreen.h"

#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "booksync/ui/account/ServiceChoice.h"
#include "components/BookSyncPresentation.h"
#include "components/UITheme.h"
#include "fontIds.h"
namespace booksync {
namespace {
namespace kit = BookSyncPresentation;
#ifdef SIMULATOR
uint32_t paints = 0;
Rect choiceTargets[4];
#endif
class ServiceChoiceActivity final : public Activity {
  ServiceChoice choice;
  Library& library;
  kit::Frame presentation() const { return kit::frame(renderer, mappedInput.hasTouch(), gpio.deviceIsX3()); }
  int titleTop() const {
    const auto f = presentation();
    return f.content().y + f.unit(26);
  }
  int bodyTop() const {
    const auto f = presentation();
    return titleTop() +
           kit::paragraphHeight(renderer, f, f.content().width, tr(STR_ACCOUNT_CHOOSE_SERVICE), kit::Type::Title) +
           f.unit(16);
  }
  int rowsTop() const {
    const auto f = presentation();
    return bodyTop() + kit::paragraphHeight(renderer, f, f.content().width, tr(STR_ACCOUNT_SETUP_LATER)) + f.unit(22);
  }
  Rect row(uint8_t index) const {
    const auto f = presentation();
    return index < 2 ? f.row(rowsTop() + index * f.unit(62)) : f.actions(rowsTop() + f.unit(146), 2).buttons[index - 2];
  }
  const char* label(uint8_t index) const {
    if (index == 0) return choice.view().serviceLabel[0] ? choice.view().serviceLabel.data() : tr(STR_HOME_UNAVAILABLE);
    if (index == 1) return tr(STR_ACCOUNT_SAVED_BOOKS);
    if (index == 2) return tr(STR_CONTINUE);
    return tr(STR_ACCOUNT_LATER);
  }

 public:
  ServiceChoiceActivity(GfxRenderer& renderer, MappedInputManager& input, Library& library)
      : Activity("ServiceChoice", renderer, input), choice(library), library(library) {}
  void onEnter() override {
    Activity::onEnter();
    choice.enter();
    requestUpdate();
  }
  void onExit() override {
    choice.retireChoice();
#ifdef SIMULATOR
    for (auto& target : choiceTargets) target = Rect{};
#endif
    Activity::onExit();
  }
  void loop() override {
    RenderLock lock;
    if (choice.pollChoice()) {
      activityManager.popActivity();
      return;
    }
    const bool selectedAccount = choice.selected() == 0;
    bool continued = false;
    bool changed = false;
    int x = 0, y = 0;
    const bool tapped = mappedInput.wasScreenTapped(x, y);
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        (tapped &&
         TouchActionButtons::indexAt(
             TouchActionButtons::vertical(presentation().back(), 1, presentation().back().height, 0), x, y) == 0)) {
      choice.retireChoice();
    } else if (mappedInput.wasReleased(mappedInput.menuButton(MappedInputManager::Button::Down))) {
      choice.stepChoice(1);
      changed = true;
    } else if (mappedInput.wasReleased(mappedInput.menuButton(MappedInputManager::Button::Up))) {
      choice.stepChoice(-1);
      changed = true;
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      continued = choice.focus() == 2;
      choice.activateChoice();
      changed = true;
    } else if (tapped) {
      for (uint8_t i = 0; i < 4; ++i) {
        const auto r = row(i);
        if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height) {
          continued = i == 2;
          choice.chooseChoice(i);
          changed = true;
          break;
        }
      }
    }
    if (!choice.live() && continued && selectedAccount)
      showAccountForm(renderer, mappedInput, library);
    else if (!choice.live())
      activityManager.popActivity();
    else if (changed)
      requestUpdate();
  }
  void render(RenderLock&&) override {
    if (!choice.live()) return;  // A queued draw cannot revive a retired presentation.
#ifdef SIMULATOR
    ++paints;
    for (uint8_t i = 0; i < 4; ++i) choiceTargets[i] = row(i);
#endif
    // All values were captured outside paint. No Library reads or dispatch here.
    renderer.clearScreen();
    const auto f = presentation();
    const auto c = f.content();
    kit::header(renderer, f, tr(STR_ACCOUNT_CHOOSE_SERVICE));
    kit::section(renderer, f, Rect{c.x, c.y, c.width, f.unit(16)}, tr(STR_ACCOUNT_READY));
    kit::paragraph(renderer, f, Rect{c.x, titleTop(), c.width, f.unit(100)}, tr(STR_ACCOUNT_CHOOSE_SERVICE),
                   kit::Type::Title);
    kit::paragraph(renderer, f, Rect{c.x, bodyTop(), c.width, f.unit(80)}, tr(STR_ACCOUNT_SETUP_LATER));
    for (uint8_t i = 0; i < 2; ++i)
      kit::row(renderer, f, row(i), label(i),
               i == 1                    ? tr(STR_ACCOUNT_NO_SIGNIN)
               : choice.view().available ? tr(STR_ACCOUNT_AVAILABLE)
                                         : tr(STR_HOME_UNAVAILABLE),
               choice.focus() < 2 && choice.focus() == i, false, choice.selected() == i);
    const char* labels[] = {label(2), label(3)};
    kit::drawActions(renderer, f, f.actions(rowsTop() + f.unit(146), 2), labels, 0,
                     choice.focus() >= 2 ? choice.focus() - 2 : -1);
    kit::footer(renderer, f, tr(STR_ACCOUNT_CHOOSE_SERVICE));
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
bool serviceChoiceControlCenter(uint8_t index, int& x, int& y) {
  RenderLock lock;
  if (index >= 4 || choiceTargets[index].width <= 0) return false;
  const auto r = choiceTargets[index];
  x = r.x + r.width / 2;
  y = r.y + r.height / 2;
  return true;
}
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
