// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace booksync {
constexpr unsigned ReminderTimeLimit = 8;
constexpr unsigned ReminderSnapshotBytes = 8192;
constexpr unsigned ReminderDraftBytes = 1024;
constexpr unsigned ReminderDueBytes = 1024;
constexpr unsigned ReminderServiceBytes = 4096;
constexpr unsigned ReminderScratchBytes = 1024;
constexpr unsigned ReminderStoreBytes = 8192;
// Support and current availability are distinct. Interval add/delete/time fields
// are deliberately absent; the singleton supports edit/enable/Done/Turn off.
struct ReminderCapabilities {
  bool time = false, interval = false, localEdit = false, snooze = false;
  bool sleep = false, clockKnown = false;
  uint8_t timeLimit = ReminderTimeLimit, messageCharacters = 60;
  uint16_t messageBytes = 240;
  uint8_t intervals[4] = {20, 30, 45, 60};
  constexpr ReminderCapabilities(bool supported = false)
      : time(supported), interval(supported), localEdit(supported), snooze(supported) {}
  constexpr operator bool() const { return time ? interval : false; }
};
enum class ReminderSource : uint8_t { Local, Managed };
enum class ReminderKind : uint8_t { Time, Interval };
enum class ReminderStorage : uint8_t { Ready, Recovered, Unreadable };
enum class ReminderStatus : uint8_t { Saved, Pending, Invalid, Stale, Limit, ReadOnly, StorageError, Unavailable };
enum class ReminderOperation : uint8_t { Add, Edit, SetEnabled, Delete };
enum class ReminderAction : uint8_t { GotIt, Snooze10, Sleep, Done, TurnOff };
struct ReminderFields {
  ReminderKind kind = ReminderKind::Time;
  bool enabled = false;
  uint16_t minute = 0;
  uint8_t days = 0;
  uint8_t intervalMinutes = 0;
  char message[241]{};
};
struct ReminderMutation {
  ReminderOperation operation = ReminderOperation::Add;
  uint64_t id = 0, revision = 0, key = 0;
  ReminderFields fields{};
};
struct ReminderResult {
  ReminderStatus status = ReminderStatus::Unavailable;
  uint64_t id = 0, revision = 0, token = 0;
  bool sleep = false;
};
struct ReminderRow {
  ReminderSource source = ReminderSource::Local;
  uint64_t id = 0, revision = 0;
  ReminderFields fields{};
  bool editable = true;
  char sourceLabel[32]{};
  char timeLabel[16]{}, daysLabel[64]{};
};
struct ReminderEvents {
  uint64_t changed = 0, clockChanged = 0, due = 0;
  ReminderKind changedKind = ReminderKind::Time, dueKind = ReminderKind::Time;
};
struct ReminderList {
  uint64_t nextMutationKey = 0;
  ReminderRow rows[9]{};
  uint8_t count = 0;
  ReminderStorage storage = ReminderStorage::Unreadable;
  bool clockKnown = false, sleepAvailable = false;
  void clear() {
    for (unsigned i = 0; i < 9; ++i) rows[i] = {};
    nextMutationKey = 0;
    count = 0;
    storage = ReminderStorage::Unreadable;
    clockKnown = sleepAvailable = false;
  }
};
static_assert(sizeof(ReminderList) <= ReminderSnapshotBytes);
struct ReminderDue {
  ReminderSource source = ReminderSource::Local;
  ReminderKind kind = ReminderKind::Time;
  uint64_t id = 0, revision = 0;
  int64_t scheduled = 0, effective = 0;
  int32_t day = 0;
  uint64_t sequence = 0, session = 0;
  bool missed = false;
  char message[241]{};
};
static_assert(sizeof(ReminderMutation) <= ReminderDraftBytes);
static_assert(sizeof(ReminderDue) <= ReminderDueBytes);
}  // namespace booksync
