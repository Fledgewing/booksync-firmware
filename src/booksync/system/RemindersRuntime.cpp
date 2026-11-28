// SPDX-License-Identifier: GPL-3.0-or-later
#include "RemindersRuntime.h"

#include "booksync/storage/RemindersStore.h"
namespace booksync {
namespace {
struct ReminderRuntime {
  storage::RemindersStore store{{"/.booksync/reminders", "/.booksync/reminders.tmp", "/.booksync/reminders.bak"}};
  Reminders service{store.port()};
  RemindersFacade facade{service};
};
// One fixed boot-lifetime workspace; never duplicated per Home/reader/account.
// No heap allocation, PSRAM requirement, task or second framebuffer is added.
// Reserve includes facade binding pointers and static-init
// guard/alignment outside this aggregate; the due presenter has its own budget.
static_assert(sizeof(ReminderRuntime) + 48 <= ReminderServiceBytes);
}  // namespace
RemindersFacade& reminderRuntime() {
  static ReminderRuntime runtime;
  return runtime.facade;
}
}  // namespace booksync
extern "C" void booksync_reminders_link(const booksync::ReminderNow& now, booksync::ReminderList& list,
                                        const booksync::ReminderMutation& mutation, booksync::ReminderDue& due,
                                        uint64_t owner, uint64_t token, booksync::ReminderAction action) {
  auto& runtime = booksync::reminderRuntime();
  runtime.work(now);
  runtime.list(list);
  runtime.mutate(mutation);
  runtime.due(due);
  runtime.claim(due, owner);
  runtime.respond(token, owner, action);
  runtime.retire(owner);
}
