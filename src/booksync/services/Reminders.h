// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/Reminders.h"
namespace booksync {
// Scalars captured outside paint. elapsed includes suspend; page excludes all
// menus/modal sheets. A resume input is processed only after gap evaluation.
struct ReminderNow {
  int64_t wall = 0;
  uint64_t elapsed = 0;
  int16_t offsetMinutes = 0;
  bool known = false, visible = false, sleeping = false, page = false, modal = false;
};
struct ReminderRecord {
  uint64_t id = 0, revision = 0, mutation = 0;
  int64_t after = 0, snooze = 0, snoozeScheduled = 0;
  int32_t highWater = -1, snoozeDay = -1;
  uint32_t sequence = 0;
  ReminderFields fields{};
};
struct ReminderData {
  ReminderRecord rows[8]{};
  uint64_t nextId = 2, intervalRevision = 1, intervalMutation = 0;
  uint64_t deletedId = 0, deletedKey = 0;
  uint8_t interval = 20;
  bool enabled = false;
};
// Worker-only serialized durable port; never exposed in Library headers.
struct RemindersPort {
  void* context;
  ReminderStorage (*load)(void*, ReminderData&);
  bool (*save)(void*, const ReminderData&);
};
class Reminders {
 public:
  explicit Reminders(RemindersPort port) : port_(port) {}
  ReminderStorage load();
  ReminderResult mutate(const ReminderMutation&, const ReminderNow&);
  bool next(const ReminderNow&, ReminderDue&);
  ReminderResult claim(const ReminderDue&, uint64_t lifetime, const ReminderNow&);
  ReminderResult respond(uint64_t token, uint64_t lifetime, ReminderAction, const ReminderNow&);
  void activity(const ReminderNow&, bool input = false);
  void retire(uint64_t lifetime);
  unsigned count() const;
  uint64_t nextMutation() const;
  bool intervalEnabled() const { return data_.enabled; }
  uint8_t intervalMinutes() const { return data_.interval; }
  const ReminderData& data() const { return data_; }
  static bool valid(const ReminderData&);
  static bool validRecord(const ReminderRecord&, uint64_t nextId);
  static bool valid(const ReminderFields&);

 private:
  RemindersPort port_;
  ReminderData data_{};
  ReminderStorage storage_ = ReminderStorage::Unreadable;
  uint64_t snoozeElapsed_[8]{};
  uint64_t activationElapsed_[8]{};
  struct Interval {
    uint64_t last = 0, idle = 0, active = 0;
    uint64_t generation = 1, sequence = 0;
    bool initialized = false, page = false, ready = false, idleReset = false;
  } interval_;
  static_assert(sizeof(Interval) <= 64);
  struct Grant {
    ReminderKind kind = ReminderKind::Time;
    uint64_t id = 0, revision = 0;
    int32_t day = -1;
    int64_t scheduled = 0;
  } grant_{};
  uint64_t owner_ = 0, token_ = 0, serial_ = 0;
  bool waiting_ = false, live_ = false;
  bool save();
  void resetInterval();
};
static_assert(sizeof(Reminders) <= ReminderServiceBytes);
}  // namespace booksync
