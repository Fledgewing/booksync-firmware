// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "FilePublish.h"
#include "booksync/services/Reminders.h"
namespace booksync::storage {
class RemindersStore {
 public:
  explicit RemindersStore(PublishPaths paths) : paths_(paths) {}
  ReminderStorage load(ReminderData&);
  bool save(const ReminderData&);
  RemindersPort port() {
    return {this, [](void* p, ReminderData& d) { return static_cast<RemindersStore*>(p)->load(d); },
            [](void* p, const ReminderData& d) { return static_cast<RemindersStore*>(p)->save(d); }};
  }

 private:
  PublishPaths paths_;
  // One fixed record scratch, not a second resident schedule or task-stack copy.
  ReminderRecord scratch_{};
  static bool validate(FsFile&, void*);
  bool read(FsFile&, ReminderData*);
};
static_assert(sizeof(RemindersStore) <= ReminderScratchBytes);
}  // namespace booksync::storage
