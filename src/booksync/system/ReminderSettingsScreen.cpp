// SPDX-License-Identifier: GPL-3.0-or-later
#include "ReminderSettingsScreen.h"

#include <AppCapabilities.h>
#include <I18n.h>
#include <Memory.h>
#include <MemoryBudget.h>

#include <cstdio>
#include <cstring>

#include "../../../lib/hal/HalElapsed.h"
#include "activities/Activity.h"
#include "booksync/services/OfflineLibrary.h"
#include "booksync/ui/reminders/Settings.h"
#include "components/UITheme.h"
#include "fontIds.h"
namespace booksync {
namespace {
constexpr StrId days[7] = {StrId::STR_REMINDER_MON, StrId::STR_REMINDER_TUE, StrId::STR_REMINDER_WED,
                           StrId::STR_REMINDER_THU, StrId::STR_REMINDER_FRI, StrId::STR_REMINDER_SAT,
                           StrId::STR_REMINDER_SUN};
struct Region {
  int x, y, w, h;
  bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};
class ReminderSettingsActivity;
#ifdef SIMULATOR
ReminderSettingsActivity* sceneSettings = nullptr;
#endif
class ReminderSettingsActivity final : public Activity {
  ReminderSettings model;
  uint8_t focus = 0, dayPage = 0, timeField = 0;
  bool numbers = false, touching = false, upperCase = false;
  uint8_t touched = 255;
  Region safe() const {
    const auto r = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    return {r.x, r.y, r.width, r.height};
  }
  Region row(unsigned i) const {
    auto r = safe();
    int top = 58, height = 48;
    if (model.screen() == ReminderScreen::List) {
      int y = r.y + 54;
      if (!model.memo().clockKnown) paintReminderText(tr(STR_REMINDER_CLOCK_HELP), r.x + 12, y, r.w - 24, false);
      if (model.memo().storage == ReminderStorage::Recovered)
        paintReminderText(tr(STR_REMINDER_RECOVERED), r.x + 12, y, r.w - 24, false);
      if (model.memo().clockKnown && model.memo().storage != ReminderStorage::Recovered)
        paintReminderText(tr(STR_REMINDER_SUBTITLE), r.x + 12, y, r.w - 24, false);
      top = y - r.y + 10;
      height = (r.h - top - (model.memo().clockKnown ? 80 : 128) - (model.pages() > 1 ? 24 : 0)) / 4;
    }
    return {r.x + 12, r.y + top + static_cast<int>(i) * height, r.w - 24, height - 4};
  }
  Region footer(unsigned i, unsigned count = 1) const {
    auto r = safe();
    const int width = (r.w - 24) / static_cast<int>(count);
    return {r.x + 12 + static_cast<int>(i) * width, r.y + r.h - 52, width - 4, 44};
  }
  Region key(unsigned i) const {
    auto r = safe();
    return {r.x + 12 + static_cast<int>(i % 10) * (r.w - 24) / 10, r.y + r.h - 184 + static_cast<int>(i / 10) * 44,
            (r.w - 24) / 10 - 2, 42};
  }
  void paintReminderText(const char* text, int x, int& y, int width, bool draw = true) const {
    char buffer[241]{};
    unsigned used = 0;
    while (*text) {
      unsigned n = 1;
      const auto c = static_cast<unsigned char>(*text);
      if (c >= 0xf0)
        n = 4;
      else if (c >= 0xe0)
        n = 3;
      else if (c >= 0xc0)
        n = 2;
      if (used + n >= sizeof(buffer)) {
        buffer[used] = 0;
        if (draw) renderer.drawText(UI_10_FONT_ID, x, y, buffer);
        y += renderer.getLineHeight(UI_10_FONT_ID) + 3;
        used = 0;
        continue;
      }
      for (unsigned i = 0; i < n && text[i]; ++i) buffer[used + i] = text[i];
      buffer[used + n] = 0;
      if (used && (used + n >= sizeof(buffer) || renderer.getTextWidth(UI_10_FONT_ID, buffer) > width)) {
        buffer[used] = 0;
        if (draw) renderer.drawText(UI_10_FONT_ID, x, y, buffer);
        y += renderer.getLineHeight(UI_10_FONT_ID) + 3;
        used = 0;
        continue;
      }
      used += n;
      text += n;
    }
    if (used) {
      if (draw) renderer.drawText(UI_10_FONT_ID, x, y, buffer);
      y += renderer.getLineHeight(UI_10_FONT_ID) + 3;
    }
  }
  void box(Region r, const char* text, bool selected, bool enabled = true) const {
    if (!enabled) renderer.fillRectDither(r.x, r.y, r.w, r.h, Color::LightGray);
    renderer.drawRect(r.x, r.y, r.w, r.h, selected && enabled ? 3 : 1, true);
    renderer.drawText(UI_10_FONT_ID, r.x + 10, r.y + 8, text);
  }
  void minutes(char* text, unsigned length, unsigned minute) const {
    const unsigned h = minute / 60, m = minute % 60;
    if (model.memo().twelveHour)
      std::snprintf(text, length, "%u:%02u %s", h % 12 ? h % 12 : 12, m,
                    I18N.get(h < 12 ? StrId::STR_AM : StrId::STR_PM));
    else
      std::snprintf(text, length, "%02u:%02u", h, m);
  }
  unsigned dayRows() const { return CROSSINK_APP_REMINDER_TEXT_LAYOUT || safe().h < 500 ? 4 : 7; }
  bool focusValid(unsigned value) const {
    if (model.screen() == ReminderScreen::Message) return messageKey(value);
    if (model.screen() == ReminderScreen::List && model.memo().storage != ReminderStorage::Unreadable) {
      if (value < 4) return model.page() * 4 + value < model.memo().count;
      if (value == 4) return model.timeCount() < ReminderTimeLimit;
      if (value == 5) return model.page() > 0;
      if (value == 6) return model.page() + 1 < model.pages();
    }
    if (model.screen() == ReminderScreen::CustomDays) {
      if (value < dayRows()) return dayPage * dayRows() + value < 7;
      if (value == dayRows() + 1) return dayPage > 0;
      if (value == dayRows() + 2) return dayPage == 0;
    }
    return true;
  }
  unsigned actions() const {
    switch (model.screen()) {
      case ReminderScreen::List:
        return model.memo().storage == ReminderStorage::Unreadable ? 2 : model.memo().clockKnown ? 7 : 8;
      case ReminderScreen::Editor:
        return !model.draft().editable                                             ? 1
               : model.draft().draftMutation.fields.kind == ReminderKind::Interval ? 3
               : model.draft().existing                                            ? 6
                                                                                   : 5;
      case ReminderScreen::Time:
        return model.memo().twelveHour ? 6 : 5;
      case ReminderScreen::Days:
        return 5;
      case ReminderScreen::CustomDays:
        return dayRows() + (dayRows() < 7 ? 3 : 1);
      case ReminderScreen::Message:
        return 40;
      case ReminderScreen::Interval:
        return 5;
      case ReminderScreen::Delete:
        return 2;
      default:
        return 0;
    }
  }
  bool messageKey(unsigned i) const {
    if (numbers) return i < 10 || i == 30 || i == 32 || i == 34 || i == 37 || i == 39;
    return i < 19 || (i >= 20 && i < 27) || i == 30 || i == 32 || i == 34 || i == 37 || i == 39;
  }
  void changeFocus(int delta) {
    const auto n = actions();
    if (!n) return;
    do {
      focus = uint8_t((static_cast<int>(focus) + delta + static_cast<int>(n)) % static_cast<int>(n));
    } while (!focusValid(focus));
  }
  void timeDigit(unsigned digit) {
    auto minute = model.draft().child.minute;
    unsigned hour = minute / 60, part = minute % 60;
    const bool twelve = model.memo().twelveHour, pm = hour >= 12;
    unsigned display = twelve ? (hour % 12 ? hour % 12 : 12) : hour;
    if (focus == 0) display = digit * 10 + display % 10;
    if (focus == 1) display = display / 10 * 10 + digit;
    if (focus == 2) part = digit * 10 + part % 10;
    if (focus == 3) part = part / 10 * 10 + digit;
    if (focus > 3 || part > 59 || display > (twelve ? 12 : 23) || (twelve && !display)) return;
    hour = twelve ? (display % 12 + (pm ? 12 : 0)) : display;
    model.setReminderMinute(uint16_t(hour * 60 + part));
  }
  void activate(bool touch = false) {
    const auto screen = model.screen();
    switch (screen) {
      case ReminderScreen::List:
        if (model.memo().storage == ReminderStorage::Unreadable) {
          if (focus == 0)
            model.backReminders();
          else
            model.retryReminders();
          break;
        }
        if (focus < 4)
          model.editReminderDraft(model.page() * 4 + focus);
        else if (focus == 4)
          model.addReminderDraft();
        else if (focus == 5 && model.page())
          model.setReminderPage(model.page() - 1);
        else if (focus == 6 && model.page() + 1 < model.pages())
          model.setReminderPage(model.page() + 1);
        else if (focus == 7)
          model.connectReminderTime();
        break;
      case ReminderScreen::Editor: {
        if (!model.draft().editable) {
          model.backReminders();
          break;
        }
        const bool interval = model.draft().draftMutation.fields.kind == ReminderKind::Interval;
        if (focus == 0)
          model.toggleReminderDraft();
        else if (interval) {
          if (focus == 1)
            model.openReminderChild(ReminderScreen::Interval);
          else
            model.saveReminderDraft();
        } else if (focus == 1)
          model.openReminderChild(ReminderScreen::Time);
        else if (focus == 2)
          model.openReminderChild(ReminderScreen::Days);
        else if (focus == 3)
          model.openReminderChild(ReminderScreen::Message);
        else if (focus == 4)
          model.saveReminderDraft();
        else
          model.requestReminderDelete();
        break;
      }
      case ReminderScreen::Time:
        if (touch && focus >= 32) {
          const unsigned k = focus - 32;
          if (k == 11)
            model.finishReminderChild();
          else if (k == 9) {
            timeField = uint8_t(timeField ? timeField - 1 : 0);
            focus = timeField;
          } else {
            focus = timeField;
            timeDigit(k == 10 ? 0 : k + 1);
            timeField = uint8_t((timeField + 1) % 4);
            focus = timeField;
          }
        } else if (touch && focus < 4) {
          timeField = focus;
        } else if (focus == actions() - 1)
          model.finishReminderChild();
        else if (focus == 4)
          model.setReminderMinute((model.draft().child.minute + 720) % 1440);
        else {
          const auto before = model.draft().child.minute;
          const unsigned h = model.memo().twelveHour ? (before / 60 % 12 ? before / 60 % 12 : 12) : before / 60;
          const unsigned current = focus == 0   ? h / 10
                                   : focus == 1 ? h % 10
                                   : focus == 2 ? before % 60 / 10
                                                : before % 10;
          for (unsigned i = 1; i <= 10; ++i) {
            timeDigit((current + i) % 10);
            if (model.draft().child.minute != before) break;
          }
        }
        break;
      case ReminderScreen::Days:
        if (focus < 3) {
          model.setReminderDays(focus == 0 ? 127 : focus == 1 ? 31 : 96);
        } else if (focus == 3)
          model.customReminderDays();
        else
          model.finishReminderChild();
        break;
      case ReminderScreen::CustomDays:
        if (focus < dayRows()) {
          const unsigned d = dayPage * dayRows() + focus;
          if (d < 7) model.setReminderDays(model.draft().child.days ^ (1 << d));
        } else if (focus == dayRows())
          model.finishReminderChild();
        else
          dayPage = uint8_t(!dayPage);
        break;
      case ReminderScreen::Interval:
        if (focus == 4)
          model.finishReminderChild();
        else {
          constexpr uint8_t values[4] = {20, 30, 45, 60};
          model.setReminderInterval(values[focus]);
        }
        break;
      case ReminderScreen::Message:
        if (focus == 30)
          model.eraseReminderMessage();
        else if (focus == 32)
          numbers = !numbers;
        else if (focus == 37)
          model.finishReminderChild();
        else if (focus == 39)
          upperCase = !upperCase;
        else if (focus == 34)
          model.appendReminderMessage({{' ', 0}});
        else {
          static constexpr char letters[] = "QWERTYUIOPASDFGHJKL";
          static constexpr char bottom[] = "ZXCVBNM";
          const char c = numbers ? char('0' + focus) : focus < 19 ? letters[focus] : bottom[focus - 20];
          model.appendReminderMessage({{!numbers && !upperCase && c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c, 0}});
        }
        break;
      case ReminderScreen::Delete:
        if (focus == 0)
          model.backReminders();
        else
          model.confirmReminderDelete();
        break;
      default:
        break;
    }
    if (screen != model.screen()) {
      focus = 0;
      dayPage = 0;
      touching = false;
    }
  }
  uint8_t target(int x, int y) const {
    const auto screen = model.screen();
    if (screen == ReminderScreen::Message) {
      if constexpr (CROSSINK_APP_REMINDER_TEXT_LAYOUT) return 255;
      for (unsigned i = 0; i < 40; ++i)
        if (messageKey(i)) {
          auto a = key(i);
          if (i >= 30 && i != 39) a.w *= i == 34 ? 3 : 2;
          if (a.contains(x, y)) return i;
        }
      return 255;
    }
    if (screen == ReminderScreen::Time) {
      const auto r = safe();
      for (unsigned i = 0; i < actions() - 1; ++i) {
        Region a{r.x + 12 + static_cast<int>(i) * (r.w - 24) / static_cast<int>(actions()), r.y + 146,
                 (r.w - 24) / static_cast<int>(actions()) - 4, 40};
        if (a.contains(x, y)) return i;
      }
      if (mappedInput.hasTouch())
        for (unsigned i = 0; i < 12; ++i) {
          Region a{r.x + 12 + static_cast<int>(i % 3) * (r.w - 24) / 3, r.y + r.h - 184 + static_cast<int>(i / 3) * 44,
                   (r.w - 24) / 3 - 4, 42};
          if (a.contains(x, y)) return 32 + i;
        }
      if (!mappedInput.hasTouch() && footer(0).contains(x, y)) return actions() - 1;
      return 255;
    }
    if (screen == ReminderScreen::List) {
      if (model.memo().storage == ReminderStorage::Unreadable) {
        for (unsigned i = 0; i < 2; ++i)
          if (footer(i, 2).contains(x, y)) return i;
      } else {
        for (unsigned i = 0; i < 4; ++i)
          if (row(i).contains(x, y) && focusValid(i)) return i;
        if (model.pages() == 1) {
          if (footer(0).contains(x, y) && focusValid(4)) return 4;
        } else
          for (unsigned i = 0; i < 3; ++i)
            if (footer(i, 3).contains(x, y) && focusValid(4 + i)) return 4 + i;
        auto connect = footer(0);
        connect.y -= 48;
        if (!model.memo().clockKnown && connect.contains(x, y)) return 7;
      }
      return 255;
    }
    if (screen == ReminderScreen::Editor) {
      if (!model.draft().editable) return footer(0).contains(x, y) ? 0 : 255;
      const bool interval = model.draft().draftMutation.fields.kind == ReminderKind::Interval;
      for (unsigned i = 0; i < (interval ? 2u : 4u); ++i)
        if (row(i).contains(x, y)) return i;
      if (interval || !model.draft().existing) return footer(0).contains(x, y) ? (interval ? 2 : 4) : 255;
      auto save = footer(0);
      save.y -= 48;
      if (save.contains(x, y)) return 4;
      return footer(0).contains(x, y) ? 5 : 255;
    }
    if (screen == ReminderScreen::CustomDays) {
      for (unsigned i = 0; i < dayRows(); ++i)
        if (row(i).contains(x, y) && focusValid(i)) return i;
      if (dayRows() == 7) return footer(0).contains(x, y) ? 7 : 255;
      for (unsigned i = 0; i < 3; ++i)
        if (footer(i, 3).contains(x, y) && focusValid(dayRows() + i)) return dayRows() + i;
      return 255;
    }
    if (screen == ReminderScreen::Delete) {
      for (unsigned i = 0; i < 2; ++i)
        if (footer(i, 2).contains(x, y)) return i;
      return 255;
    }
    if (screen == ReminderScreen::Days || screen == ReminderScreen::Interval) {
      for (unsigned i = 0; i < 4; ++i)
        if (row(i).contains(x, y)) return i;
      if (footer(0).contains(x, y)) return 4;
    }
    return 255;
  }

 public:
  ReminderSettingsActivity(GfxRenderer& r, MappedInputManager& input, Library& library)
      : Activity("Reminders", r, input), model(library) {}
#ifdef SIMULATOR
  ~ReminderSettingsActivity() override {
    if (sceneSettings == this) sceneSettings = nullptr;
  }
  void prepareScene(unsigned state) {
    model.enterReminders();
    focus = 0;
    dayPage = 0;
    if ((state >= 4 && state <= 12) || state == 17 || state == 21) {
      model.addReminderDraft();
      if (state == 5 || state == 17) model.openReminderChild(ReminderScreen::Time);
      if (state == 21) {
        model.openReminderChild(ReminderScreen::Days);
        model.customReminderDays();
        dayPage = CROSSINK_APP_REMINDER_TEXT_LAYOUT ? 1 : 0;
      }
      if (state == 6) model.openReminderChild(ReminderScreen::Days);
      if (state == 7 || state == 8) {
        model.openReminderChild(ReminderScreen::Days);
        model.customReminderDays();
        if (state == 8) {
          model.setReminderDays(0);
          model.finishReminderChild();
        }
      }
      if (state == 9) model.openReminderChild(ReminderScreen::Message);
      if (state == 10) model.saveReminderDraft();
      if (state == 12) model.toggleReminderDraft();
      if (state == 11) {
        model.saveReminderDraft();
        model.pollReminders();
      }
    }
    if (state == 13) {
      model.editReminderDraft(1);
      model.requestReminderDelete();
    }
    if (state == 14) {
      model.editReminderDraft(0);
    }
    if (state == 15) {
      model.editReminderDraft(0);
      model.openReminderChild(ReminderScreen::Interval);
    }
    if (state == 16) {
      model.editReminderDraft(1);
    }
    requestUpdate();
  }
  bool exerciseSceneInput(unsigned which) {
    const auto touchAction = [this](Region a) {
      const auto hit = target(a.x + a.w / 2, a.y + a.h / 2);
      if (hit == 255) return false;
      focus = hit;
      activate(true);
      return true;
    };
    if (which == 0) {
      prepareScene(4);
      if (!touchAction(footer(0))) return false;
      return model.screen() == ReminderScreen::Saving;
    }
    if (which == 1) {
      prepareScene(14);
      focus = 0;
      activate();
      if (!touchAction(footer(0))) return false;
      return model.screen() == ReminderScreen::Saving;
    }
    if (which == 2 || which == 3) {
      prepareScene(13);
      if (focus != 0) return false;
      if (!touchAction(footer(which == 2 ? 0 : 1, 2))) return false;
      return model.screen() == (which == 2 ? ReminderScreen::Editor : ReminderScreen::Saving);
    }
    if (which == 4 || which == 10) {
      prepareScene(which == 10 ? 17 : 5);
      if (mappedInput.hasTouch()) {
        const auto r = safe();
        Region field{r.x + 12, r.y + 146, (r.w - 24) / static_cast<int>(actions()) - 4, 40};
        if (!touchAction(field)) return false;
        Region zero{r.x + 12 + (r.w - 24) / 3, r.y + r.h - 184 + 3 * 44, (r.w - 24) / 3 - 4, 42};
        if (!touchAction(zero)) return false;
        Region done{r.x + 12 + 2 * (r.w - 24) / 3, r.y + r.h - 184 + 3 * 44, (r.w - 24) / 3 - 4, 42};
        if (!touchAction(done)) return false;
      } else {
        focus = 1;
        activate();
        focus = actions() - 1;
        activate();
      }
      return model.screen() == ReminderScreen::Editor && model.draft().draftMutation.fields.minute < 1440;
    }
    if (which == 5) {
      prepareScene(9);
      focus = 0;
      activate();
      if (!model.draft().child.message[0]) return false;
      focus = 37;
      activate();
      return model.screen() == ReminderScreen::Editor;
    }
    if (which == 6) {
      prepareScene(8);
      focus = dayRows();
      activate();
      return model.screen() == ReminderScreen::CustomDays && model.error() == ReminderStatus::Invalid;
    }
    if (which == 7) {
      prepareScene(7);
      if (dayRows() == 4) {
        dayPage = 1;
        focus = 2;
      } else
        focus = 6;
      activate();
      focus = dayRows();
      activate();
      return model.screen() == ReminderScreen::Editor && model.draft().draftMutation.fields.days == 63;
    }
    if (which == 8) {
      prepareScene(0);
      focus = 3;
      changeFocus(1);
      return focus == 6 && target(footer(0, 3).x + 8, footer(0, 3).y + 8) == 255;
    }
    if (which == 9) {
      prepareScene(16);
      if (model.draft().editable) return false;
      const auto a = row(0);
      if (target(a.x + 8, a.y + 8) != 255) return false;
      if (!touchAction(footer(0))) return false;
      return model.screen() == ReminderScreen::List;
    }
    return false;
  }

#endif
  bool blocksGlobalInput() const override { return model.screen() != ReminderScreen::List; }
  bool allowsReminderOverlay() const override { return model.screen() == ReminderScreen::List; }
  void onReminderClockResult() override {
    if (model.screen() == ReminderScreen::List) {
      model.enterReminders();
      focus = 0;
      requestUpdate();
    }
  }
  void onEnter() override {
    Activity::onEnter();
    const auto started = HalElapsed::milliseconds();
    model.enterReminders();
    MemoryBudget::logHeapShape("reminders.settings.enter");
    LOG_DBG("BSR", "span=reminders.settings.acquire ms=%llu snapshot=%u draft=%u rows=%u",
            static_cast<unsigned long long>(HalElapsed::milliseconds() - started), unsigned(sizeof(ReminderList)),
            unsigned(sizeof(ReminderDraft)), unsigned(model.memo().count));
    focus = 0;
    requestUpdate();
  }
  void loop() override {
    RenderLock lock(RenderLock::Mode::Try);
    if (!lock.ownsLock()) return;
    using B = MappedInputManager::Button;
    if (model.pollReminders()) {
      focus = 0;
      requestUpdate();
    }
    if (mappedInput.wasPressed(B::Back)) {
      mappedInput.suppressNextBackRelease();
      model.backReminders();
      focus = 0;
      requestUpdate();
    } else if (mappedInput.wasPressed(B::Left) || mappedInput.wasPressed(B::Up) ||
               mappedInput.wasPressed(B::PageBack)) {
      changeFocus(-1);
      requestUpdate();
    } else if (mappedInput.wasPressed(B::Right) || mappedInput.wasPressed(B::Down) ||
               mappedInput.wasPressed(B::PageForward)) {
      changeFocus(1);
      requestUpdate();
    } else if (mappedInput.wasPressed(B::Confirm)) {
      activate();
      requestUpdate();
    }
    int x = 0, y = 0;
    if (mappedInput.wasScreenTouchDown(x, y)) {
      touched = target(x, y);
      touching = true;
    }
    if (mappedInput.wasScreenTapped(x, y)) {
      if (touching && target(x, y) == touched && touched != 255) {
        focus = touched;
        activate(true);
        requestUpdate();
      }
      touching = false;
      const auto r = safe();
      if (x < r.x + 100 && y < r.y + 42) {
        model.backReminders();
        focus = 0;
        requestUpdate();
      }
    } else if (mappedInput.wasScreenTouchReleased())
      touching = false;
    if (!model.live()) finish();
  }
  void render(RenderLock&&) override {
    // Memo-only paint. No Library requests, storage, JSON, clocks or networking.
    renderer.clearScreen();
    const auto r = safe();
    const auto screen = model.screen();
    renderer.drawText(UI_10_FONT_ID, r.x + 12, r.y + 8, tr(STR_BACK));
    const auto& d = model.draft().draftMutation.fields;
    const bool interval = d.kind == ReminderKind::Interval;
    StrId title = StrId::STR_REMINDERS;
    if (screen == ReminderScreen::Editor)
      title = interval                 ? StrId::STR_REMINDER_EYE_NAME
              : model.draft().existing ? StrId::STR_REMINDER_EDIT
                                       : StrId::STR_REMINDER_ADD;
    if (screen == ReminderScreen::Time) title = StrId::STR_REMINDER_TIME;
    if (screen == ReminderScreen::Days || screen == ReminderScreen::CustomDays) title = StrId::STR_REMINDER_DAYS;
    if (screen == ReminderScreen::Message) title = StrId::STR_REMINDER_MESSAGE;
    if (screen == ReminderScreen::Interval) title = StrId::STR_REMINDER_INTERVAL;
    if (screen == ReminderScreen::Delete) title = StrId::STR_REMINDER_DELETE_TITLE;
    if (screen == ReminderScreen::Saving) title = StrId::STR_REMINDER_SAVING;
    renderer.drawText(BITTER_14_FONT_ID, r.x + 12, r.y + 30, I18N.get(title));
    // Message (240 UTF-8 bytes) plus bounded day/source labels.
    char text[384]{};
    if (screen == ReminderScreen::List) {
      if (model.memo().storage == ReminderStorage::Unreadable) {
        int y = r.y + 100;
        paintReminderText(tr(STR_REMINDER_UNREADABLE), r.x + 12, y, r.w - 24);
        paintReminderText(tr(STR_REMINDER_DATA_KEPT), r.x + 12, y, r.w - 24);
        box(footer(0, 2), tr(STR_BACK), focus == 0);
        box(footer(1, 2), tr(STR_RETRY), focus == 1);
      } else {
        int y = r.y + 54;
        if (!model.memo().clockKnown) paintReminderText(tr(STR_REMINDER_CLOCK_HELP), r.x + 12, y, r.w - 24);
        if (model.memo().storage == ReminderStorage::Recovered)
          paintReminderText(tr(STR_REMINDER_RECOVERED), r.x + 12, y, r.w - 24);
        if (model.memo().clockKnown && model.memo().storage != ReminderStorage::Recovered)
          paintReminderText(tr(STR_REMINDER_SUBTITLE), r.x + 12, y, r.w - 24);
        for (unsigned i = 0; i < 4; ++i) {
          const unsigned index = model.page() * 4 + i;
          if (index >= model.memo().count) break;
          const auto& value = model.memo().rows[index];
          const auto a = row(i);
          std::snprintf(text, sizeof(text), tr(STR_REMINDER_ROW_FORMAT),
                        value.fields.kind == ReminderKind::Interval ? tr(STR_REMINDER_EYE_NAME) : value.timeLabel,
                        value.source != ReminderSource::Local ? tr(STR_REMINDER_FAMILY)
                        : value.fields.enabled                ? tr(STR_REMINDER_ON)
                                                              : tr(STR_REMINDER_OFF));
          if constexpr (CROSSINK_APP_REMINDER_TEXT_LAYOUT) {
            char numbered[96]{};
            std::snprintf(numbered, sizeof(numbered), tr(STR_REMINDER_NUMBERED_FORMAT), index + 1, text);
            box(a, numbered, focus == i);
          } else
            box(a, text, focus == i);
          if (value.fields.kind == ReminderKind::Interval) {
            if (!model.memo().clockKnown)
              std::snprintf(text, sizeof(text), "%s", tr(STR_REMINDER_EYE_CLOCK_HELP));
            else
              std::snprintf(text, sizeof(text), tr(STR_REMINDER_EYE_SUBTITLE_FORMAT),
                            unsigned(value.fields.intervalMinutes));
          } else if (!model.memo().clockKnown)
            std::snprintf(text, sizeof(text), "%s", tr(STR_REMINDER_WAITING));
          else
            std::snprintf(text, sizeof(text), tr(STR_REMINDER_ROW_FORMAT), value.daysLabel,
                          value.fields.message[0] ? value.fields.message : tr(STR_REMINDER_DEFAULT));
          int detailY = a.y + renderer.getLineHeight(UI_10_FONT_ID) + 10;
          paintReminderText(text, a.x + 10, detailY, a.w - 20);
        }
        if (!model.timeCount()) {
          int y = row(1).y;
          paintReminderText(tr(STR_REMINDER_EMPTY), r.x + 12, y, r.w - 24);
          paintReminderText(tr(STR_REMINDER_EMPTY_HELP), r.x + 12, y, r.w - 24);
          paintReminderText(tr(STR_REMINDER_LOCAL_ACCESS), r.x + 12, y, r.w - 24);
        }
        if (model.pages() > 1) {
          const unsigned first = model.page() * 4,
                         stop = first + 4 < model.memo().count ? first + 4 : model.memo().count;
          std::snprintf(text, sizeof(text), tr(STR_REMINDER_PAGING_FORMAT), first ? first : 1, stop - 1,
                        unsigned(model.timeCount()));
          renderer.drawText(UI_10_FONT_ID, r.x + 12, r.y + r.h - (model.memo().clockKnown ? 102 : 150), text);
        }
        std::snprintf(text, sizeof(text), tr(STR_REMINDER_COUNT_FORMAT), unsigned(model.timeCount()));
        renderer.drawText(UI_10_FONT_ID, r.x + 12, r.y + r.h - (model.memo().clockKnown ? 78 : 126),
                          model.timeCount() == 8 ? tr(STR_REMINDER_FULL) : text);
        if (model.pages() == 1)
          box(footer(0), tr(STR_REMINDER_ADD), focus == 4, focusValid(4));
        else {
          box(footer(0, 3), tr(STR_REMINDER_ADD), focus == 4, focusValid(4));
          box(footer(1, 3), tr(STR_HOME_PREV), focus == 5, focusValid(5));
          box(footer(2, 3), tr(STR_HOME_NEXT), focus == 6, focusValid(6));
        }
        if (!model.memo().clockKnown) {
          auto connect = footer(0);
          connect.y -= 48;
          box(connect, tr(STR_REMINDER_CONNECT), focus == 7);
        }
      }
    } else if (screen == ReminderScreen::Editor) {
      if (!model.draft().editable) {
        int y = r.y + 80;
        paintReminderText(tr(STR_REMINDER_MANAGED), r.x + 12, y, r.w - 24);
        paintReminderText(tr(STR_REMINDER_READ_ONLY), r.x + 12, y, r.w - 24);
        char time[24]{};
        minutes(time, sizeof(time), d.minute);
        std::snprintf(text, sizeof(text), tr(STR_REMINDER_FIELD_FORMAT), tr(STR_REMINDER_TIME), time);
        paintReminderText(text, r.x + 12, y, r.w - 24);
        paintReminderText(I18N.get(d.days == 127  ? StrId::STR_REMINDER_EVERY_DAY
                                   : d.days == 31 ? StrId::STR_REMINDER_WEEKDAYS
                                   : d.days == 96 ? StrId::STR_REMINDER_WEEKENDS
                                                  : StrId::STR_REMINDER_CUSTOM),
                          r.x + 12, y, r.w - 24);
        paintReminderText(d.message[0] ? d.message : tr(STR_REMINDER_DEFAULT), r.x + 12, y, r.w - 24);
        box(footer(0), tr(STR_BACK), true);
      } else {
        std::snprintf(text, sizeof(text), tr(STR_REMINDER_FIELD_FORMAT), tr(STR_REMINDER),
                      d.enabled ? tr(STR_REMINDER_ON) : tr(STR_REMINDER_OFF));
        box(row(0), text, focus == 0);
        minutes(text, sizeof(text), d.minute);
        char label[96]{};
        std::snprintf(label, sizeof(label), tr(STR_REMINDER_FIELD_FORMAT), tr(STR_REMINDER_TIME), text);
        if (interval) std::snprintf(label, sizeof(label), tr(STR_REMINDER_MINUTES_FORMAT), unsigned(d.intervalMinutes));
        box(row(1), label, focus == 1);
        if (!interval) {
          std::snprintf(label, sizeof(label), tr(STR_REMINDER_FIELD_FORMAT), tr(STR_REMINDER_DAYS),
                        I18N.get(d.days == 127  ? StrId::STR_REMINDER_EVERY_DAY
                                 : d.days == 31 ? StrId::STR_REMINDER_WEEKDAYS
                                 : d.days == 96 ? StrId::STR_REMINDER_WEEKENDS
                                                : StrId::STR_REMINDER_CUSTOM));
          box(row(2), label, focus == 2);
          std::snprintf(label, sizeof(label), tr(STR_REMINDER_FIELD_FORMAT), tr(STR_REMINDER_MESSAGE),
                        d.message[0] ? d.message : tr(STR_REMINDER_DEFAULT));
          box(row(3), renderer.getTextWidth(UI_10_FONT_ID, label) < row(3).w - 20 ? label : tr(STR_REMINDER_MESSAGE),
              focus == 3);
        } else {
          int y = row(2).y;
          paintReminderText(tr(STR_REMINDER_INTERVAL_HELP), r.x + 12, y, r.w - 24);
        }
        int y = r.y + r.h - 156;
        if (model.error() != ReminderStatus::Saved && model.error() != ReminderStatus::Pending)
          paintReminderText(tr(STR_REMINDER_SAVE_ERROR), r.x + 12, y, r.w - 24);
        auto save = footer(0);
        if (!interval && model.draft().existing) save.y -= 48;
        box(save, tr(STR_REMINDER_SAVE), focus == (interval ? 2 : 4));
        if (!interval && model.draft().existing) box(footer(0), tr(STR_REMINDER_DELETE), focus == 5);
      }
    } else if (screen == ReminderScreen::Days || screen == ReminderScreen::CustomDays) {
      if (screen == ReminderScreen::Days) {
        constexpr StrId labels[4] = {StrId::STR_REMINDER_EVERY_DAY, StrId::STR_REMINDER_WEEKDAYS,
                                     StrId::STR_REMINDER_WEEKENDS, StrId::STR_REMINDER_CUSTOM};
        for (unsigned i = 0; i < 4; ++i) box(row(i), I18N.get(labels[i]), focus == i);
        box(footer(0), tr(STR_DONE), focus == 4);
      } else {
        for (unsigned i = 0; i < dayRows(); ++i) {
          const unsigned n = dayPage * dayRows() + i;
          if (n >= 7) break;
          std::snprintf(text, sizeof(text), "[%c] %s", model.draft().child.days & (1 << n) ? 'x' : ' ',
                        I18N.get(days[n]));
          box(row(i), text, focus == i);
        }
        if (dayRows() == 7)
          box(footer(0), tr(STR_DONE), focus == 7);
        else {
          box(footer(0, 3), tr(STR_DONE), focus == 4);
          box(footer(1, 3), tr(STR_HOME_PREV), focus == 5, focusValid(5));
          box(footer(2, 3), tr(STR_HOME_NEXT), focus == 6, focusValid(6));
        }
      }
      if (model.error() == ReminderStatus::Invalid)
        renderer.drawText(UI_10_FONT_ID, r.x + 12, r.y + r.h - 80, tr(STR_REMINDER_DAY_REQUIRED));
    } else if (screen == ReminderScreen::Interval) {
      constexpr uint8_t values[4] = {20, 30, 45, 60};
      for (unsigned i = 0; i < 4; ++i) {
        std::snprintf(text, sizeof(text), tr(STR_REMINDER_MINUTES_FORMAT), unsigned(values[i]));
        box(row(i), text, focus == i);
      }
      box(footer(0), tr(STR_DONE), focus == 4);
    } else if (screen == ReminderScreen::Delete) {
      int y = r.y + 86;
      paintReminderText(tr(STR_REMINDER_DELETE_HELP), r.x + 12, y, r.w - 24);
      if (model.error() != ReminderStatus::Saved) paintReminderText(tr(STR_REMINDER_SAVE_ERROR), r.x + 12, y, r.w - 24);
      box(footer(0, 2), tr(STR_CANCEL), focus == 0);
      box(footer(1, 2), tr(STR_REMINDER_DELETE), focus == 1);
    } else if (screen == ReminderScreen::Time) {
      minutes(text, sizeof(text), model.draft().child.minute);
      int y = r.y + 62;
      paintReminderText(tr(STR_REMINDER_TIME_HELP), r.x + 12, y, r.w - 24);
      renderer.drawText(BITTER_14_FONT_ID, r.x + 12, r.y + 90, text);
      renderer.drawText(UI_10_FONT_ID, r.x + 12, r.y + 122,
                        I18N.get(model.memo().twelveHour ? StrId::STR_REMINDER_TIME12 : StrId::STR_REMINDER_TIME24));
      const auto minute = model.draft().child.minute;
      const unsigned h = model.memo().twelveHour ? (minute / 60 % 12 ? minute / 60 % 12 : 12) : minute / 60;
      for (unsigned i = 0; i < actions() - 1; ++i) {
        Region a{r.x + 12 + static_cast<int>(i) * (r.w - 24) / static_cast<int>(actions()), r.y + 146,
                 (r.w - 24) / static_cast<int>(actions()) - 4, 40};
        const unsigned digit = i == 0 ? h / 10 : i == 1 ? h % 10 : i == 2 ? minute % 60 / 10 : minute % 10;
        std::snprintf(text, sizeof(text), "%u", digit);
        box(a, i == 4 ? I18N.get(minute >= 720 ? StrId::STR_PM : StrId::STR_AM) : text, focus == i);
      }
      if (mappedInput.hasTouch()) {
        for (unsigned i = 0; i < 12; ++i) {
          Region a{r.x + 12 + static_cast<int>(i % 3) * (r.w - 24) / 3, r.y + r.h - 184 + static_cast<int>(i / 3) * 44,
                   (r.w - 24) / 3 - 4, 42};
          std::snprintf(text, sizeof(text), "%u", i == 10 ? 0 : i + 1);
          box(a, i == 9 ? tr(STR_REMINDER_BACKSPACE) : i == 11 ? tr(STR_DONE) : text, false);
        }
      } else
        box(footer(0), tr(STR_DONE), focus == actions() - 1);
    } else if (screen == ReminderScreen::Message) {
      int y = r.y + 62;
      paintReminderText(tr(STR_REMINDER_MESSAGE_LIMIT), r.x + 12, y, r.w - 24);
      paintReminderText(model.draft().child.message[0] ? model.draft().child.message : tr(STR_REMINDER_MESSAGE_HELP),
                        r.x + 12, y, r.w - 24);
      unsigned selectedOrdinal = 0;
      for (unsigned i = 0; i < focus; ++i)
        if (messageKey(i)) ++selectedOrdinal;
      const unsigned textPage = selectedOrdinal / 4;
      unsigned ordinal = 0;
      for (unsigned i = 0; i < 40; ++i)
        if (messageKey(i)) {
          text[0] = 0;
          const char* caption = text;
          if (i == 30)
            caption = tr(STR_REMINDER_BACKSPACE);
          else if (i == 32)
            caption = numbers ? tr(STR_REMINDER_LETTERS) : tr(STR_REMINDER_NUMBERS);
          else if (i == 34)
            caption = tr(STR_REMINDER_SPACE);
          else if (i == 37)
            caption = tr(STR_DONE);
          else if (i == 39)
            caption = tr(STR_REMINDER_CASE);
          else {
            static constexpr char letters[] = "QWERTYUIOPASDFGHJKL";
            static constexpr char bottom[] = "ZXCVBNM";
            text[0] = numbers ? char('0' + i) : i < 19 ? letters[i] : bottom[i - 20];
            text[1] = 0;
          }
          if constexpr (CROSSINK_APP_REMINDER_TEXT_LAYOUT) {
            if (ordinal / 4 == textPage) {
              Region a{r.x + 12, r.y + r.h - 224 + static_cast<int>(ordinal % 4) * 44, r.w - 24, 42};
              char numbered[96]{};
              std::snprintf(numbered, sizeof(numbered), tr(STR_REMINDER_NUMBERED_FORMAT), ordinal + 1, caption);
              box(a, numbered, focus == i);
            }
            ++ordinal;
          } else {
            auto a = key(i);
            if (i >= 30 && i != 39) a.w *= i == 34 ? 3 : 2;
            box(a, caption, focus == i);
          }
        }
    }
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
};
}  // namespace
void showReminderSettings(GfxRenderer& renderer, MappedInputManager& input) {
  // One fallible lifecycle-owned screen: bounded snapshot + draft, no task or
  // framebuffer. Static storage would retain a closed editor and its draft.
  auto screen = makeUniqueNoThrow<ReminderSettingsActivity>(renderer, input, offlineLibrary());
  if (!screen) {
    LOG_ERR("BSR", "Cannot allocate reminder settings");
    return;
  }
  activityManager.pushActivity(std::move(screen));
}
}  // namespace booksync

#ifdef SIMULATOR
namespace booksync {
void showReminderSettingsScene(GfxRenderer& renderer, MappedInputManager& input, Library& library) {
  auto screen = makeUniqueNoThrow<ReminderSettingsActivity>(renderer, input, library);
  if (!screen) return;
  sceneSettings = screen.get();
  activityManager.pushActivity(std::move(screen));
}
bool inputReminderSettingsScene(unsigned which) { return sceneSettings && sceneSettings->exerciseSceneInput(which); }
void prepareReminderSettingsScene(unsigned state) {
  if (sceneSettings) sceneSettings->prepareScene(state);
}
}  // namespace booksync
#endif
