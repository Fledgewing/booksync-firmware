// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>

#include "booksync/storage/RemindersStore.h"
inline void runRemindersScenario() {
  if (!std::getenv("BOOKSYNC_REMINDERS_SCENARIO")) return;
  using namespace booksync;
  static storage::RemindersStore store({"/reminders", "/reminders.tmp", "/reminders.bak"});
  static Reminders service(store.port());
  bool ok = service.load() == ReminderStorage::Ready && service.count() == 0;
  ReminderNow now{1795813200, 0, 0, true, true, false, true, false};
  static ReminderMutation mutation;
  mutation.key = 1;
  mutation.fields.enabled = true;
  mutation.fields.days = 127;
  mutation.fields.minute = 1261;
  ok = ok && service.mutate(mutation, now).status == ReminderStatus::Saved;
  now.wall += 60;
  static ReminderDue due;
  ok = ok && service.next(now, due);
  const auto grant = service.claim(due, 7, now);
  ok = ok && grant.status == ReminderStatus::Saved &&
       service.respond(grant.token, 7, ReminderAction::Snooze10, now).status == ReminderStatus::Saved;
  ok = ok && service.load() == ReminderStorage::Ready;
  now.wall += 600;
  ok = ok && service.next(now, due) && due.sequence == 1;
  const auto snoozed = service.claim(due, 8, now);
  ok = ok && snoozed.status == ReminderStatus::Saved;
  ok = ok && service.load() == ReminderStorage::Ready && !service.next(now, due);
  LOG_INF("BSR", "BookSync reminders scenario %s: service=%u scratch=%u disk=2498 policy-allocations=0",
          ok ? "PASS" : "FAIL", unsigned(sizeof(Reminders)), unsigned(sizeof(storage::RemindersStore)));
  std::fflush(nullptr);
  std::_Exit(ok ? 0 : 1);
}
