// SPDX-License-Identifier: GPL-3.0-or-later
#include "ReminderPresenter.h"

#include <AppCapabilities.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <atomic>
#include <ctime>
#include <optional>

#include "../../../lib/hal/HalElapsed.h"
#include "CrossPointSettings.h"
#include "GfxRenderer.h"
#include "MappedInputManager.h"
#include "ReminderClaimRetry.h"
#include "activities/Activity.h"
#include "activities/RenderLock.h"
#include "booksync/services/LocalLibrary.h"
#include "booksync/services/OfflineLibrary.h"
#include "booksync/services/RemindersFormat.h"
#include "booksync/system/ClockRuntime.h"
#include "booksync/system/ReadingGate.h"
#include "booksync/system/RemindersRuntime.h"
#include "booksync/ui/reminders/DueSheet.h"
#include "components/UITheme.h"
#include "fontIds.h"
namespace booksync {
namespace {
#ifdef SIMULATOR
uint64_t sceneInputCount = 0;
#endif
struct Presenter {
  explicit Presenter(Library& library) : sheet(library) {}
  ReminderDueSheet sheet;
  ReminderClaimRetry retry;
  ReminderNow now{};
  uint64_t lifetime = 1, lastEvaluation = 0, revision = 0, changes = 0;
  bool initialized = false, hidden = false, input = false, claiming = false, sleep = false, connectPending = false;
  std::atomic<uint32_t> frame{0};
};
static_assert(sizeof(Presenter) <= ReminderDueBytes);
Presenter& presenter() {
  static Presenter value{offlineLibrary()};
  return value;
}
void queueReminderClock() { presenter().connectPending = true; }
void observeActivity(ReminderActivity value) {
  auto now = presenter().now;
  now.elapsed = value.elapsed;
  now.page = value.page;
  now.visible = value.visible;
  now.sleeping = value.sleeping;
  now.modal = value.modal;
  reminderRuntime().observe(now, value.input);  // Pure scalar adoption, no disk or scheduling scan.
}
void reportActivity(bool input) {
  const auto& p = presenter();
  offlineLibrary().remindersActivity(
      {HalElapsed::milliseconds(), p.now.page, p.now.visible && !p.hidden, p.hidden, p.now.modal, input});
}
void format(ReminderPresentation& value, const ReminderNow& now) {
  formatReminderPresentation(value, now,
                             {tr(STR_HOME_ON_DEVICE), tr(STR_REMINDER_DEFAULT), tr(STR_REMINDER_EYE_MESSAGE),
                              tr(STR_REMINDER_DUE_FORMAT), tr(STR_AM), tr(STR_PM), SETTINGS.clockFormat != 0});
}
void formatList(ReminderList& listValue) {
  formatReminderList(listValue,
                     {tr(STR_HOME_ON_DEVICE),
                      tr(STR_REMINDER_EVERY_DAY),
                      tr(STR_REMINDER_WEEKDAYS),
                      tr(STR_REMINDER_WEEKENDS),
                      {tr(STR_REMINDER_MON), tr(STR_REMINDER_TUE), tr(STR_REMINDER_WED), tr(STR_REMINDER_THU),
                       tr(STR_REMINDER_FRI), tr(STR_REMINDER_SAT), tr(STR_REMINDER_SUN)},
                      tr(STR_AM),
                      tr(STR_PM),
                      SETTINGS.clockFormat == 1});
}
// Named main-loop service span; no network/task callback performs this work.
void evaluateReminders(RemindersFacade& facade, const ReminderNow& now, bool input = false) {
  const auto started = HalElapsed::milliseconds();
  facade.work(now, input);
  LOG_DBG("BSR", "span=reminders.evaluate ms=%llu schedules<=8 interval=1",
          static_cast<unsigned long long>(HalElapsed::milliseconds() - started));
}
bool requestSleep(void*) {
  if (!activityManager.checkpointReminderSleep()) return false;
  presenter().sleep = true;
  return true;
}
struct SheetRect {
  int x, y, width, height, actionY, actionHeight;
};
SheetRect geometry(GfxRenderer& renderer, const ReminderSheetView& value) {
  const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int wanted = (value.due.kind == ReminderKind::Time ? 400 : 300) +
                     (value.error == ReminderSheetError::None ? 0 : 48) +
                     (value.due.kind == ReminderKind::Time && !value.sleepAvailable ? 40 : 0);
  const int height = std::min(safe.height - 24, wanted);
  const int actions = value.due.kind == ReminderKind::Time ? 174 : 122;
  return {safe.x + 12,
          safe.y + (safe.height - height) / 2,
          safe.width - 24,
          height,
          safe.y + (safe.height - height) / 2 + height - actions,
          48};
}
uint8_t hit(GfxRenderer& renderer, const ReminderSheetView& value, int x, int y, unsigned count) {
  const auto r = geometry(renderer, value);
  if (x < r.x + 12 || x >= r.x + r.width - 12) return 255;
  for (unsigned i = 0; i < count; ++i)
    if (y >= r.actionY + int(i) * 52 && y < r.actionY + int(i) * 52 + r.actionHeight) return i;
  return 255;
}
// One bounded line, UTF-8 boundaries preserved. Never clips a message silently.
void paragraph(GfxRenderer& renderer, int x, int& y, int width, const char* text) {
  char line[241]{};
  size_t used = 0;
  while (*text) {
    unsigned bytes = 1;
    const auto c = static_cast<unsigned char>(*text);
    if (c >= 0xf0)
      bytes = 4;
    else if (c >= 0xe0)
      bytes = 3;
    else if (c >= 0xc0)
      bytes = 2;
    if (used + bytes >= sizeof(line)) {
      line[used] = 0;
      renderer.drawText(UI_10_FONT_ID, x, y, line);
      y += renderer.getLineHeight(UI_10_FONT_ID) + 4;
      used = 0;
      continue;
    }
    for (unsigned i = 0; i < bytes && text[i]; ++i) line[used + i] = text[i];
    line[used + bytes] = 0;
    if (used && renderer.getTextWidth(UI_10_FONT_ID, line) > width) {
      line[used] = 0;
      renderer.drawText(UI_10_FONT_ID, x, y, line);
      y += renderer.getLineHeight(UI_10_FONT_ID) + 4;
      used = 0;
      continue;
    }
    used += bytes;
    text += bytes;
  }
  if (used) {
    renderer.drawText(UI_10_FONT_ID, x, y, line);
    y += renderer.getLineHeight(UI_10_FONT_ID) + 4;
  }
}
void paint(GfxRenderer& renderer, const ReminderSheetView& value) {
  const auto r = geometry(renderer, value);
  renderer.fillRect(r.x, r.y, r.width, r.height, false);
  renderer.drawRect(r.x, r.y, r.width, r.height, 2, true);
  int y = r.y + 12;
  renderer.drawText(
      BITTER_14_FONT_ID, r.x + 14, y,
      I18N.get(value.due.kind == ReminderKind::Time ? StrId::STR_REMINDER : StrId::STR_REMINDER_EYE_TITLE));
  y += renderer.getLineHeight(BITTER_14_FONT_ID) + 8;
  renderer.drawText(UI_10_FONT_ID, r.x + 14, y, value.sourceLabel);
  y += renderer.getLineHeight(UI_10_FONT_ID) + 8;
  if (value.dueLabel[0]) paragraph(renderer, r.x + 14, y, r.width - 28, value.dueLabel);
  paragraph(renderer, r.x + 14, y, r.width - 28, value.due.message);
  if (!value.sleepAvailable && value.due.kind == ReminderKind::Time)
    paragraph(renderer, r.x + 14, y, r.width - 28, tr(STR_REMINDER_SLEEP_UNAVAILABLE));
  if (value.error != ReminderSheetError::None)
    paragraph(renderer, r.x + 14, y, r.width - 28,
              I18N.get(value.error == ReminderSheetError::Sleep   ? StrId::STR_REMINDER_SLEEP_ERROR
                       : value.due.kind == ReminderKind::Interval ? StrId::STR_REMINDER_TURN_OFF_ERROR
                                                                  : StrId::STR_REMINDER_SAVE_ERROR));
  const unsigned count = value.due.kind == ReminderKind::Time ? 3 : 2;
  for (unsigned i = 0; i < count; ++i) {
    const auto label = value.due.kind == ReminderKind::Time
                           ? (i == 0   ? StrId::STR_REMINDER_GOT_IT
                              : i == 1 ? StrId::STR_REMINDER_SNOOZE
                                       : StrId::STR_SLEEP)
                           : (i == 0                                        ? StrId::STR_DONE
                              : value.due.source == ReminderSource::Managed ? StrId::STR_REMINDER_MANAGED_OFF
                                                                            : StrId::STR_REMINDER_TURN_OFF);
    const int top = r.actionY + int(i) * 52;
    const bool enabled =
        !(value.due.kind == ReminderKind::Time && i == 2 && !value.sleepAvailable) &&
        !(value.due.kind == ReminderKind::Interval && i == 1 && value.due.source != ReminderSource::Local);
    if (!enabled) renderer.fillRectDither(r.x + 12, top, r.width - 24, r.actionHeight, Color::LightGray);
    renderer.drawRect(r.x + 12, top, r.width - 24, r.actionHeight, value.focus == i && enabled ? 3 : 1, true);
    renderer.drawText(UI_10_FONT_ID, r.x + 24, top + 10, I18N.get(label));
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
void inputs(Presenter& p, GfxRenderer& renderer, MappedInputManager& input) {
  using B = MappedInputManager::Button;
  bool held = false;
  for (unsigned i = 0; i < MappedInputManager::BUTTON_COUNT; ++i) held |= input.isPhysicalPressed(static_cast<B>(i));
  int x = 0, y = 0;
  held |= input.isScreenTouchHeld(x, y);
  if (p.sheet.draining()) {
    if (!held) p.sheet.sheetInput(ReminderSheetInput::ReleaseAll);
    return;  // The opening release is never a fresh activation.
  }
  if (input.wasPressed(B::PageBack) || input.wasPressed(B::PageForward)) {
    input.suppressNextSideRelease(B::PageBack);
    input.suppressNextSideRelease(B::PageForward);
    p.sheet.sheetInput(ReminderSheetInput::PageTurn);
  } else if (input.wasPressed(B::Back)) {
    input.suppressNextBackRelease();
    p.sheet.sheetInput(ReminderSheetInput::Back);
  } else {
    MappedInputManager::SwipeDir swipe;
    int sx = 0, sy = 0, ex = 0, ey = 0;
    if (input.wasSwipeWithPoints(swipe, sx, sy, ex, ey)) {
      p.sheet.sheetInput(ReminderSheetInput::PageTurn);
      return;
    }
    const bool previous = input.wasPressed(B::Left) || input.wasPressed(B::Up);
    const bool next = input.wasPressed(B::Right) || input.wasPressed(B::Down);
    if (previous) p.sheet.sheetInput(ReminderSheetInput::FocusPrevious);
    if (next) p.sheet.sheetInput(ReminderSheetInput::FocusNext);
    // Duplicate configured bindings must not focus and confirm on one press.
    if (!previous && !next) {
      if (input.wasPressed(B::Confirm)) p.sheet.sheetInput(ReminderSheetInput::SelectDown);
      if (input.wasReleased(B::Confirm)) p.sheet.sheetInput(ReminderSheetInput::SelectUp);
    }
    const unsigned count = p.sheet.view().due.kind == ReminderKind::Time ? 3 : 2;
    if (input.wasScreenTouchDown(x, y))
      p.sheet.sheetInput(ReminderSheetInput::TouchDown, hit(renderer, p.sheet.view(), x, y, count));
    if (input.wasScreenTapped(x, y)) {
      const auto action = hit(renderer, p.sheet.view(), x, y, count);
      if (action == 255)
        p.sheet.sheetInput(ReminderSheetInput::OutsideTap);
      else
        p.sheet.sheetInput(ReminderSheetInput::TouchUp, action);
    } else if (input.wasScreenTouchReleased())
      p.sheet.sheetInput(ReminderSheetInput::TouchUp, 255);
  }
}
// Keep the due-value workspace off the durable worker call chain.
[[gnu::noinline]] bool updateReminderSheet(Presenter& p, RemindersFacade& facade, Library& library,
                                           GfxRenderer& renderer, MappedInputManager& input,
                                           std::optional<RenderLock>& claimLock) {
  const bool hadFrame = p.frame.load();
  if (!p.now.visible || p.now.sleeping || p.now.modal) {
    if (hadFrame || p.claiming) reminderNavigation();
    return hadFrame;
  }
  if (p.sheet.view().open) {
    const auto changes = library.remindersEvents().changed;
    if (changes != p.changes && !p.sheet.view().responsePending &&
        !library.remindersPresentation(p.sheet.sheetClaimValue()).ready) {
      p.sheet.retireSheet();
      p.frame = false;
      activityManager.requestUpdate();
      return true;
    }
    p.changes = changes;
    inputs(p, renderer, input);
    p.sheet.pollSheet();
    if (!p.sheet.view().open) {
      p.sheet.retireSheet();
      p.frame = false;
      activityManager.requestUpdate();
    } else if (p.revision != p.sheet.revision()) {
      RenderLock lock;
      paint(renderer, p.sheet.view());  // Only fresh, explicit input/error redraws.
      p.revision = p.sheet.revision();
    }
    return true;
  }
  if (p.claiming) {
    const auto result = library.remindersResult();
    if (result.status == ReminderStatus::Pending) return false;
    p.claiming = false;
    const auto& due = p.sheet.view().due;
    ReminderClaim claim{due.source,   due.kind,   due.id,      due.revision,
                        result.token, p.lifetime, due.session, due.sequence};
    if (result.status != ReminderStatus::Saved) {
      p.retry.failed(result.status, due, p.now.elapsed);
      return false;
    }
    if (!claimLock) claimLock.emplace();
    sampleRuntimeClock();
    p.now.known = library.clockStatus().known();
    p.now.wall = p.now.known ? int64_t(std::time(nullptr)) : 0;
    p.now.elapsed = HalElapsed::milliseconds();
    const auto fresh = activityManager.reminderContext();
    p.now.visible = fresh.visible && !p.hidden;
    p.now.page = fresh.page && !p.hidden;
    p.now.modal = fresh.modal;
    evaluateReminders(facade, p.now);  // No pending work: proof/expiry adoption only, no I/O.
    const auto value = library.remindersPresentation(claim);
    auto permit = ReadingGate::reminderPermit(library, claim, {p.now.page, p.now.visible, p.hidden});
    if (!value.ready ||
        !ReadingGate::drawReminder(
            permit,
            {{p.now.page, p.now.visible, p.hidden}, p.lifetime, p.now.elapsed, p.now.known, p.now.page, p.now.modal}) ||
        !p.sheet.enterSheet(value, claim, library.reminderSleepAvailable())) {
      library.remindersRetire(p.lifetime);
      return false;
    }
    p.frame = true;
    p.changes = library.remindersEvents().changed;
    paint(renderer, p.sheet.view());
    p.revision = p.sheet.revision();
    return true;
  }
  ReminderDue due;
  if (library.remindersDue(due) && p.retry.allows(due, p.now.elapsed) && p.sheet.stageSheet(due))
    p.claiming = library.remindersClaim(due, p.lifetime).status == ReminderStatus::Pending;
  return false;
}
}  // namespace
bool reminderOwnsFrame() { return presenter().frame.load(); }
void reminderHidden(bool hidden) {
  auto& p = presenter();
  if (p.hidden == hidden) return;
  p.hidden = hidden;
  reminderNavigation();
}
void reminderNavigation() {
  auto& p = presenter();
  p.connectPending = false;
  p.sheet.retireSheet();
  if (p.claiming) offlineLibrary().remindersRetire(p.lifetime);
  p.claiming = false;
  p.retry.reset();
  p.frame = false;
  if (p.lifetime != UINT64_MAX)
    ++p.lifetime;
  else
    p.hidden = true;
  p.now.page = false;
  reportActivity(false);
}
void reminderInput() {
#ifdef SIMULATOR
  ++sceneInputCount;
#endif
  presenter().input = true;
  reportActivity(true);
}
bool takeReminderSleep() {
  auto& p = presenter();
  const bool value = p.sleep;
  p.sleep = false;
  return value;
}
bool reminderLoop(GfxRenderer& renderer, MappedInputManager& input, bool page, bool visible, bool modal) {
  if constexpr (!CROSSINK_APP_CAP_REMINDERS) return false;
  auto& p = presenter();
  auto& facade = reminderRuntime();
  auto& library = offlineLibrary();
  if (!p.initialized) {
    bindReminders(facade);
    bindReminderConnect(queueReminderClock);
    bindReminderActivity(observeActivity);
    facade.configureFormatter(format);
    facade.configureListFormatter(formatList);
    facade.configureSleep({requestSleep, nullptr});
    facade.sleepAvailable(true);
    p.initialized = true;
  }
  if (p.connectPending) {
    p.connectPending = false;
    activityManager.connectReminderClock();
    return false;
  }
  sampleRuntimeClock();
  const bool known = library.clockStatus().known();
  const uint64_t elapsed = HalElapsed::milliseconds();
  const int64_t wall = known ? int64_t(std::time(nullptr)) : 0;
  const int16_t offset =
      int16_t((static_cast<int>(SETTINGS.clockUtcOffsetQ <= 104 ? SETTINGS.clockUtcOffsetQ : 48) - 48) * 15);
  const int64_t predicted = p.now.wall + (elapsed >= p.now.elapsed ? int64_t((elapsed - p.now.elapsed) / 1000) : 0);
  const bool corrected = known && p.now.known && (wall > predicted + 1 || wall < predicted - 1);
  const bool activePage = page && !p.hidden && !p.sheet.view().open;
  const bool changed = p.now.page != activePage || p.now.visible != (visible && !p.hidden) || p.now.modal != modal ||
                       p.now.known != known || p.now.offsetMinutes != offset || corrected;
  p.now = {wall, elapsed, offset, known, visible && !p.hidden, p.hidden, activePage, modal};
  // Observe idle before resumed input. Keep a claim's native frame exclusion
  // through its initial draw; a queued render can never replace that parent in
  // the gap between a durable claim and the one-use gate.
  std::optional<RenderLock> claimLock;
  reportActivity(p.input);
  if (changed || p.input || facade.hasReminderCommand() || !p.lastEvaluation ||
      p.now.elapsed - p.lastEvaluation >= 60000) {
    if (facade.claimCommand()) {
      claimLock.emplace();
      const auto fresh = activityManager.reminderContext();
      p.now.visible = fresh.visible && !p.hidden;
      p.now.page = fresh.page && !p.hidden;
      p.now.modal = fresh.modal;
      p.now.elapsed = HalElapsed::milliseconds();
      sampleRuntimeClock();
      p.now.known = library.clockStatus().known();
      p.now.wall = p.now.known ? int64_t(std::time(nullptr)) : 0;
      evaluateReminders(facade, p.now, p.input);
    } else if (facade.storageCommand()) {
      RenderLock storageLock;  // Exclude the native page/cache file reader.
      evaluateReminders(facade, p.now, p.input);
    } else {
      // Pure ticks/responses never wait for rendering. Sleep preflight takes
      // the reader's own non-recursive lock and queues the existing native path.
      evaluateReminders(facade, p.now, p.input);
    }
    p.lastEvaluation = p.now.elapsed;
  }
  p.input = false;
  // Large presentation values exist only after the storage worker returns, so
  // their stack workspace never overlaps mutation/codec scratch on C3.
  return updateReminderSheet(p, facade, library, renderer, input, claimLock);
}
}  // namespace booksync
#ifdef SIMULATOR
namespace booksync {
uint64_t reminderSceneInputCount() { return sceneInputCount; }
bool reminderSceneHidden() { return presenter().hidden; }
void paintReminderScene(GfxRenderer& renderer, Library& library, unsigned state) {
  ReminderDueSheet sheet{library};
  ReminderPresentation value{};
  value.ready = true;
  value.due.id = 2;
  value.due.revision = 1;
  std::snprintf(value.sourceLabel, sizeof(value.sourceLabel), "%s", tr(STR_HOME_ON_DEVICE));
  std::snprintf(value.due.message, sizeof(value.due.message), "%s", tr(STR_REMINDER_DEFAULT));
  if (state == 1) std::snprintf(value.dueLabel, sizeof(value.dueLabel), tr(STR_REMINDER_DUE_FORMAT), "9:00 PM");
  if (state >= 4 && state <= 6) {
    value.due.kind = ReminderKind::Interval;
    std::snprintf(value.due.message, sizeof(value.due.message), "%s", tr(STR_REMINDER_EYE_MESSAGE));
  }
  if (state == 6) {
    value.due.source = ReminderSource::Managed;
    std::snprintf(value.sourceLabel, sizeof(value.sourceLabel), "%s", tr(STR_REMINDER_FAMILY));
  }
  if (state == 7)
    std::snprintf(value.due.message, sizeof(value.due.message),
                  "A quiet place to finish this chapter and get ready for sleep.");
  ReminderClaim claim{value.due.source, value.due.kind, 2, 1, 1, 7, 0, 0};
  sheet.enterSheet(value, claim, state != 2);
  sheet.sheetInput(ReminderSheetInput::ReleaseAll);
  if (state == 3 || state == 5) {
    sheet.sheetInput(ReminderSheetInput::FocusNext);
    if (state == 3) sheet.sheetInput(ReminderSheetInput::FocusNext);
    sheet.sheetInput(ReminderSheetInput::SelectDown);
    sheet.sheetInput(ReminderSheetInput::SelectUp);
    sheet.pollSheet();
  }
  paint(renderer, sheet.view());  // Scene caller holds RenderLock across capture.
}
bool inputReminderScene(GfxRenderer& renderer, MappedInputManager& input, Library& library, unsigned state) {
  using B = MappedInputManager::Button;
  const bool menu = input.isMenuMode();
  input.setMenuMode(false);
  input.simulatorClearInputFrame();
  Presenter p{library};
  ReminderDue due{};
  due.id = 2;
  due.revision = 1;
  if (state == 8) due.kind = ReminderKind::Interval;
  p.sheet.enterSheet(due, {ReminderSource::Local, due.kind, 2, 1, 1, 7, 0, 0}, true);
  input.simulatorInjectPress(B::Confirm);
  inputs(p, renderer, input);
  input.simulatorClearInputFrame();
  input.simulatorInjectRelease(B::Confirm);
  inputs(p, renderer, input);
  if (!p.sheet.view().open || p.sheet.draining()) {
    input.setMenuMode(menu);
    return false;
  }
  auto key = [&](B button) {
    input.simulatorClearInputFrame();
    input.simulatorInjectPress(button);
    inputs(p, renderer, input);
    input.simulatorClearInputFrame();
    input.simulatorInjectRelease(button);
    inputs(p, renderer, input);
  };
  if (state >= 1 && state <= 5) {
    key(B::Right);
    if (state != 1) key(B::Right);
  }
  if (state <= 2)
    key(B::Confirm);
  else if (state == 3)
    key(B::PageBack);
  else if (state == 4)
    key(B::PageForward);
  else if (state == 5)
    key(B::Back);
#if CROSSINK_APP_CAP_TOUCH
  else if (state == 6 || state == 7) {
    const auto r = geometry(renderer, p.sheet.view());
    const int x = state == 6 ? r.x - 4 : r.x + r.width / 2;
    const int y = state == 6 ? r.y - 4 : r.actionY + 2 * 52 + 10;
    input.simulatorClearInputFrame();
    input.simulatorInjectTouchDown(x, state == 7 ? r.y + 20 : y);
    inputs(p, renderer, input);
    input.simulatorClearInputFrame();
    if (state == 7) input.simulatorInjectTouchMove(x, y);
    input.simulatorInjectTouchRelease(x, y);
    inputs(p, renderer, input);
  }
#endif
  else if (state == 8) {
    key(B::Right);
    key(B::Confirm);
  }
  const bool okay = state == 7 ? p.sheet.view().open : !p.sheet.view().open;
  p.sheet.retireSheet();
  input.simulatorClearInputFrame();
  input.setMenuMode(menu);
  return okay;
}
}  // namespace booksync
#endif
