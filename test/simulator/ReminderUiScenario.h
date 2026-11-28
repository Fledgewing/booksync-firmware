// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "activities/Activity.h"
#include "booksync/system/Bootstrap.h"
#include "booksync/system/ReminderPresenter.h"
#include "booksync/system/ReminderSettingsScreen.h"
#include "booksync/ui/reminders/Settings.h"
#include "fontIds.h"
extern GfxRenderer renderer;
extern MappedInputManager mappedInputManager;
namespace reminderUiScenario {
using namespace booksync;
class Fixture final : public Library {
 public:
  ReminderList value{};
  mutable unsigned reads = 0;
  unsigned writes = 0;
  ReminderStatus response = ReminderStatus::Pending;
  ReminderAction selected = ReminderAction::GotIt;
  Capabilities capabilities() const override { return {}; }
  Snapshot snapshot() const override { return {}; }
  CommandStatus submit(Intent) override {
    ++writes;
    return CommandStatus::Accepted;
  }
  void remindersList(ReminderList& out) const override {
    ++reads;
    out = value;
  }
  ReminderResult remindersMutate(const ReminderMutation&) override {
    ++writes;
    return {ReminderStatus::Pending};
  }
  ReminderResult remindersResult() const override {
    ++reads;
    return {response};
  }
  ReminderResult remindersRespond(uint64_t, uint64_t, ReminderAction action) override {
    ++writes;
    selected = action;
    return {response, 2, 1, 1, response == ReminderStatus::Saved && action == ReminderAction::Sleep};
  }
  bool remindersRetry() override { return true; }
  void configure(unsigned state) {
    value.clear();
    value.count = state == 0 ? 1 : state == 20 ? 9 : 2;
    value.nextMutationKey = 1;
    value.storage = state == 3    ? ReminderStorage::Recovered
                    : state == 19 ? ReminderStorage::Unreadable
                                  : ReminderStorage::Ready;
    value.clockKnown = state != 2;
    value.sleepAvailable = true;
    value.twelveHour = state != 17;
    value.rows[0].id = 1;
    value.rows[0].revision = 1;
    value.rows[0].fields.kind = ReminderKind::Interval;
    value.rows[0].fields.intervalMinutes = 20;
    value.rows[0].fields.enabled = state == 18;
    for (unsigned i = 1; i < value.count; ++i) {
      auto& row = value.rows[i];
      row.id = i + 1;
      row.revision = 1;
      row.fields.enabled = true;
      row.fields.minute = 1260;
      row.fields.days = 127;
      std::snprintf(row.timeLabel, sizeof(row.timeLabel), "9:00 PM");
      std::snprintf(row.daysLabel, sizeof(row.daysLabel), "Every day");
      std::snprintf(row.sourceLabel, sizeof(row.sourceLabel), "On this device");
    }
    if (state == 16) {
      value.rows[1].source = ReminderSource::Managed;
      value.rows[1].editable = false;
      std::snprintf(value.rows[1].sourceLabel, sizeof(value.rows[1].sourceLabel), "Family");
    }
    response = state == 11 ? ReminderStatus::StorageError : ReminderStatus::Pending;
  }
};
inline void pixels(const char* directory, const char* name) {
  std::filesystem::create_directories(directory);
  const auto path = std::filesystem::path(directory) / (std::string(name) + ".pgm");
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) std::_Exit(51);
  std::fprintf(f, "P5\n%d %d\n255\n", renderer.getScreenWidth(), renderer.getScreenHeight());
  for (int y = 0; y < renderer.getScreenHeight(); ++y)
    for (int x = 0; x < renderer.getScreenWidth(); ++x) std::fputc(renderer.isPixelBlack(x, y) ? 0 : 255, f);
  if (std::fclose(f)) std::_Exit(52);
}
inline bool tick() {
  const char* directory = std::getenv("BOOKSYNC_REMINDER_UI");
  if (!directory) return false;
  static Fixture fixture;
  static unsigned frame = 0, caseIndex = 0;
  const unsigned f = frame++;
  mappedInputManager.simulatorClearInputFrame();
  if (f == 0) {
    showHomeSettings(renderer, mappedInputManager);
  }
  if (f == 4) {
    if (activityManager.requestUpdateAndWait() != RequestUpdateResult::Rendered) std::_Exit(59);
    RenderLock lock;
    pixels(directory, "settings-entry");
  }
  if (f == 6 && !selectHomeReminderEntryScene()) std::_Exit(60);
  if (f == 10) {
    if (!activityManager.isCurrentActivityNamed("Reminders")) std::_Exit(61);
    if (activityManager.requestUpdateAndWait() != RequestUpdateResult::Rendered) std::_Exit(62);
    RenderLock lock;
    pixels(directory, "settings-real-entry");
  }
  if (f == 12) activityManager.popActivity();
  if (f == 16) {
    fixture.configure(0);
    showReminderSettingsScene(renderer, mappedInputManager, fixture);
  }
  if (f >= 24 && caseIndex < 22) {
    if ((f - 24) % 8 == 0) {
      fixture.configure(caseIndex);
      prepareReminderSettingsScene(caseIndex);
    }
    if ((f - 24) % 8 == 4) {
      const auto reads = fixture.reads, writes = fixture.writes;
      if (activityManager.requestUpdateAndWait() != RequestUpdateResult::Rendered) std::_Exit(53);
      RenderLock lock;
      if (reads != fixture.reads || writes != fixture.writes) std::_Exit(54);
      char name[32]{};
      std::snprintf(name, sizeof(name), "settings-%02u", caseIndex);
      pixels(directory, name);
      ++caseIndex;
    }
  }
  if (f == 210) {
    for (unsigned i = 0; i < 8; ++i) {
      fixture.response = i == 3 || i == 5 ? ReminderStatus::Unavailable : ReminderStatus::Saved;
      RenderLock lock;
      renderer.clearScreen();
      for (int y = 30; y < renderer.getScreenHeight(); y += 60)
        renderer.drawText(BITTER_14_FONT_ID, 18, y, "A sample reading page.");
      const auto reads = fixture.reads, writes = fixture.writes;
      // Preparation may respond to an explicit synthetic action; paint itself
      // is checked separately by the production renderer's memo-only contract.
      paintReminderScene(renderer, fixture, i);
      if (i != 3 && i != 5 && (reads != fixture.reads || writes != fixture.writes)) std::_Exit(55);
      char name[32]{};
      std::snprintf(name, sizeof(name), "due-%02u", i);
      pixels(directory, name);
    }
    unsigned settingsInputs = 0;
    for (unsigned i = 0; i < 11; ++i) {
      fixture.configure(i == 8 ? 20 : i == 9 ? 16 : i == 10 ? 17 : 1);
      fixture.writes = 0;
      {
        RenderLock lock;
        if (!inputReminderSettingsScene(i)) std::_Exit(63);
      }
      if (fixture.writes != unsigned(i == 0 || i == 1 || i == 3)) std::_Exit(64);
      ++settingsInputs;
    }
    unsigned inputs = 0;
    for (unsigned i = 0; i < 9; ++i) {
      if ((i == 6 || i == 7) && !mappedInputManager.hasTouch()) continue;
      if (i == 5 && mappedInputManager.hasTouch()) continue;
      fixture.response = ReminderStatus::Saved;
      fixture.writes = 0;
      if (!inputReminderScene(renderer, mappedInputManager, fixture, i)) {
        LOG_ERR("BSR", "Reminder input route failed: %u", i);
        std::_Exit(56);
      }
      const auto expected = i == 1   ? ReminderAction::Snooze10
                            : i == 2 ? ReminderAction::Sleep
                            : i == 8 ? ReminderAction::TurnOff
                                     : ReminderAction::GotIt;
      if (i == 7 ? fixture.writes > 1 || (fixture.writes && fixture.selected != ReminderAction::GotIt)
                 : fixture.writes != 1 || fixture.selected != expected) {
        LOG_ERR("BSR", "Reminder input effect failed: route=%u writes=%u action=%u", i, fixture.writes,
                unsigned(fixture.selected));
        std::_Exit(57);
      }
      ++inputs;
    }
    LOG_INF("BSR", "BookSync R1 reminder UI PASS: captures=32 input-routes=%u settings-input=%u snapshot=%u draft=%u",
            inputs, settingsInputs, unsigned(sizeof(ReminderList)), unsigned(sizeof(ReminderDraft)));
    std::fflush(nullptr);
    std::_Exit(0);
  }
  return true;
}
}  // namespace reminderUiScenario
