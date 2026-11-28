// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/Library.h"
namespace booksync {
struct ReminderGlyph {
  char utf8Value[5]{};
};
enum class ReminderScreen : uint8_t { List, Editor, Time, Days, CustomDays, Message, Interval, Delete, Saving };
struct ReminderDraft {
  ReminderMutation draftMutation{};
  ReminderFields child{};
  bool existing = false, editable = true;
};
static_assert(sizeof(ReminderDraft) <= ReminderDraftBytes);
// One bounded lifecycle snapshot and one draft. Paint uses these memo values;
// mutations are dispatched only from explicit Save/Delete, never per keystroke.
class ReminderSettings {
 public:
  explicit ReminderSettings(Library& library) : library_(library) {}
  auto enterReminders() -> void;
  auto pollReminders() -> bool;
  auto backReminders() -> void;
  auto addReminderDraft() -> bool;
  auto editReminderDraft(unsigned rowIndex) -> bool;
  auto openReminderChild(ReminderScreen) -> void;
  auto finishReminderChild() -> bool;
  auto customReminderDays() -> void;
  auto toggleReminderDraft() -> void;
  auto setReminderMinute(uint16_t) -> void;
  auto setReminderDays(uint8_t) -> void;
  auto setReminderInterval(uint8_t) -> void;
  auto appendReminderMessage(ReminderGlyph) -> bool;
  auto eraseReminderMessage() -> void;
  auto saveReminderDraft() -> bool;
  auto requestReminderDelete() -> void;
  auto confirmReminderDelete() -> bool;
  auto retryReminders() -> bool;
  auto connectReminderTime() -> void;
  auto setReminderPage(uint8_t) -> void;
  const ReminderList& memo() const { return memo_; }
  const ReminderDraft& draft() const { return draft_; }
  ReminderScreen screen() const { return screen_; }
  ReminderStatus error() const { return error_; }
  bool live() const { return live_; }
  uint8_t page() const { return page_; }
  uint8_t pages() const { return uint8_t((memo_.count + 3) / 4); }
  uint8_t timeCount() const { return memo_.count ? uint8_t(memo_.count - 1) : 0; }

 private:
  Library& library_;
  ReminderList memo_{};
  ReminderDraft draft_{};
  ReminderScreen screen_ = ReminderScreen::List;
  ReminderStatus error_ = ReminderStatus::Saved;
  uint8_t page_ = 0;
  bool live_ = false, deleting_ = false, retryWaiting_ = false;
  auto dispatchReminderDraft(ReminderOperation) -> bool;
};
}  // namespace booksync
