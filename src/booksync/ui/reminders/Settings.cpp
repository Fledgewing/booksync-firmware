// SPDX-License-Identifier: GPL-3.0-or-later
#include "booksync/ui/reminders/Settings.h"
namespace booksync {
void ReminderSettings::enterReminders() {
  library_.remindersList(memo_);
  screen_ = ReminderScreen::List;
  error_ = ReminderStatus::Saved;
  page_ = 0;
  live_ = true;
}
bool ReminderSettings::pollReminders() {
  if (!live_) return false;
  if (retryWaiting_ && screen_ == ReminderScreen::List) {
    retryWaiting_ = false;
    library_.remindersList(memo_);
    return true;
  }
  if (screen_ != ReminderScreen::Saving) return false;
  const auto saved = library_.remindersResult();
  if (saved.status == ReminderStatus::Pending) return false;
  error_ = saved.status;
  if (saved.status == ReminderStatus::Saved) {
    library_.remindersList(memo_);
    page_ = page_ < pages() ? page_ : 0;
    screen_ = ReminderScreen::List;
  } else
    screen_ = deleting_ ? ReminderScreen::Delete : ReminderScreen::Editor;
  return true;
}
void ReminderSettings::backReminders() {
  if (!live_) return;
  error_ = ReminderStatus::Saved;
  switch (screen_) {
    case ReminderScreen::List:
      live_ = false;
      break;
    case ReminderScreen::Saving:
      live_ = false;
      break;  // Device effect remains worker-owned.
    case ReminderScreen::Editor:
      screen_ = ReminderScreen::List;
      break;
    case ReminderScreen::Delete:
      screen_ = ReminderScreen::Editor;
      break;
    case ReminderScreen::CustomDays:
      screen_ = ReminderScreen::Days;
      break;
    default:
      screen_ = ReminderScreen::Editor;
      break;  // Child edits are discarded.
  }
}
bool ReminderSettings::addReminderDraft() {
  if (!live_ || screen_ != ReminderScreen::List || timeCount() >= ReminderTimeLimit ||
      memo_.storage == ReminderStorage::Unreadable)
    return false;
  draft_ = {};
  draft_.draftMutation.key = memo_.nextMutationKey;
  draft_.draftMutation.fields.enabled = true;
  draft_.draftMutation.fields.minute = 21 * 60;
  draft_.draftMutation.fields.days = 127;
  screen_ = ReminderScreen::Editor;
  error_ = ReminderStatus::Saved;
  return true;
}
bool ReminderSettings::editReminderDraft(unsigned rowIndex) {
  if (!live_ || screen_ != ReminderScreen::List || rowIndex >= memo_.count ||
      memo_.storage == ReminderStorage::Unreadable)
    return false;
  const auto& rowValue = memo_.rows[rowIndex];
  draft_ = {};
  draft_.existing = true;
  draft_.editable = rowValue.editable && rowValue.source == ReminderSource::Local;
  draft_.draftMutation.id = rowValue.id;
  draft_.draftMutation.revision = rowValue.revision;
  draft_.draftMutation.key = memo_.nextMutationKey;
  draft_.draftMutation.fields = rowValue.fields;
  screen_ = ReminderScreen::Editor;
  error_ = ReminderStatus::Saved;
  return true;
}
void ReminderSettings::openReminderChild(ReminderScreen childScreen) {
  if (screen_ != ReminderScreen::Editor || !draft_.editable) return;
  const bool interval = draft_.draftMutation.fields.kind == ReminderKind::Interval;
  if (interval ? childScreen != ReminderScreen::Interval
               : childScreen != ReminderScreen::Time && childScreen != ReminderScreen::Days &&
                     childScreen != ReminderScreen::Message)
    return;
  draft_.child = draft_.draftMutation.fields;
  screen_ = childScreen;
  error_ = ReminderStatus::Saved;
}
void ReminderSettings::customReminderDays() {
  if (screen_ == ReminderScreen::Days) screen_ = ReminderScreen::CustomDays;
}
bool ReminderSettings::finishReminderChild() {
  if (screen_ != ReminderScreen::Time && screen_ != ReminderScreen::Days && screen_ != ReminderScreen::CustomDays &&
      screen_ != ReminderScreen::Message && screen_ != ReminderScreen::Interval)
    return false;
  if ((screen_ == ReminderScreen::Days || screen_ == ReminderScreen::CustomDays) && !draft_.child.days) {
    error_ = ReminderStatus::Invalid;
    return false;
  }
  draft_.draftMutation.fields = draft_.child;
  screen_ = ReminderScreen::Editor;
  error_ = ReminderStatus::Saved;
  return true;
}
void ReminderSettings::toggleReminderDraft() {
  if (screen_ == ReminderScreen::Editor && draft_.editable)
    draft_.draftMutation.fields.enabled = !draft_.draftMutation.fields.enabled;
}
void ReminderSettings::setReminderMinute(uint16_t minuteValue) {
  if (screen_ == ReminderScreen::Time && minuteValue < 1440) draft_.child.minute = minuteValue;
}
void ReminderSettings::setReminderDays(uint8_t daysValue) {
  if ((screen_ == ReminderScreen::Days || screen_ == ReminderScreen::CustomDays) && daysValue <= 127)
    draft_.child.days = daysValue;
}
void ReminderSettings::setReminderInterval(uint8_t intervalValue) {
  if (screen_ == ReminderScreen::Interval &&
      (intervalValue == 20 || intervalValue == 30 || intervalValue == 45 || intervalValue == 60))
    draft_.child.intervalMinutes = intervalValue;
}
bool ReminderSettings::appendReminderMessage(ReminderGlyph textValue) {
  if (screen_ != ReminderScreen::Message || !textValue.utf8Value[0]) return false;
  unsigned textSize = 0;
  while (textSize < 5 && textValue.utf8Value[textSize]) ++textSize;
  if (textSize == 5) return false;
  unsigned bytes = 0, characters = 0;
  for (; draft_.child.message[bytes]; ++bytes)
    if ((static_cast<unsigned char>(draft_.child.message[bytes]) & 0xc0) != 0x80) ++characters;
  if (bytes + textSize > 240) return false;
  unsigned offset = 0, added = 0;
  while (offset < textSize) {
    const auto lead = static_cast<unsigned char>(textValue.utf8Value[offset]);
    unsigned glyphBytes = lead < 0x80                    ? 1
                          : lead >= 0xc2 && lead <= 0xdf ? 2
                          : lead <= 0xef && lead >= 0xe0 ? 3
                          : lead >= 0xf0 && lead <= 0xf4 ? 4
                                                         : 0;
    if (!glyphBytes || lead < 32 || lead == 127 || offset + glyphBytes > textSize) return false;
    uint32_t cp = lead & (glyphBytes == 1 ? 127 : glyphBytes == 2 ? 31 : glyphBytes == 3 ? 15 : 7);
    for (unsigned i = 1; i < glyphBytes; ++i) {
      const auto part = static_cast<unsigned char>(textValue.utf8Value[offset + i]);
      if ((part & 0xc0) != 0x80) return false;
      cp = (cp << 6) | (part & 63);
    }
    if ((glyphBytes == 2 && cp < 128) || (glyphBytes == 3 && cp < 2048) || (glyphBytes == 4 && cp < 65536) ||
        cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) || (cp >= 0x80 && cp <= 0x9f) || cp == 0x2028 || cp == 0x2029)
      return false;
    offset += glyphBytes;
    ++added;
  }
  if (characters + added > 60) return false;
  for (unsigned i = 0; i < textSize; ++i) draft_.child.message[bytes++] = textValue.utf8Value[i];
  draft_.child.message[bytes] = 0;
  return true;
}
void ReminderSettings::eraseReminderMessage() {
  if (screen_ != ReminderScreen::Message) return;
  unsigned bytes = 0;
  while (draft_.child.message[bytes]) ++bytes;
  if (!bytes) return;
  do {
    --bytes;
  } while (bytes && (static_cast<unsigned char>(draft_.child.message[bytes]) & 0xc0) == 0x80);
  draft_.child.message[bytes] = 0;
}
bool ReminderSettings::dispatchReminderDraft(ReminderOperation operationValue) {
  if (!draft_.editable) {
    error_ = ReminderStatus::ReadOnly;
    return false;
  }
  draft_.draftMutation.operation = operationValue;
  const auto dispatchState = library_.remindersMutate(draft_.draftMutation);
  error_ = dispatchState.status;
  if (dispatchState.status != ReminderStatus::Pending && dispatchState.status != ReminderStatus::Saved) return false;
  deleting_ = operationValue == ReminderOperation::Delete;
  screen_ = ReminderScreen::Saving;
  return true;
}
bool ReminderSettings::saveReminderDraft() {
  if (screen_ != ReminderScreen::Editor) return false;
  return dispatchReminderDraft(draft_.existing ? ReminderOperation::Edit : ReminderOperation::Add);
}
void ReminderSettings::requestReminderDelete() {
  if (screen_ == ReminderScreen::Editor && draft_.existing && draft_.editable &&
      draft_.draftMutation.fields.kind == ReminderKind::Time)
    screen_ = ReminderScreen::Delete;
}
bool ReminderSettings::confirmReminderDelete() {
  return screen_ == ReminderScreen::Delete && dispatchReminderDraft(ReminderOperation::Delete);
}
bool ReminderSettings::retryReminders() {
  if (screen_ != ReminderScreen::List || memo_.storage != ReminderStorage::Unreadable || !library_.remindersRetry())
    return false;
  retryWaiting_ = true;
  // Retry dispatches worker work. A later lifecycle acquisition adopts its value.
  return true;
}
void ReminderSettings::connectReminderTime() {
  if (live_ && screen_ == ReminderScreen::List && !memo_.clockKnown) library_.submit(Intent::ConnectReminderTime);
}
void ReminderSettings::setReminderPage(uint8_t pageValue) {
  if (screen_ == ReminderScreen::List && pageValue < pages()) page_ = pageValue;
}
}  // namespace booksync
