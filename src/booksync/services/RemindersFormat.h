// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Reminders.h"
namespace booksync {
struct ReminderWords {
  const char* onDevice;
  const char* defaultMessage;
  const char* eyeMessage;
  const char* dueFormat;
  const char* am;
  const char* pm;
  bool twelveHour;
};
// Pure bounded formatting, outside paint. Translated words come from the
// composition root; no globals, language loads, clock reads or I/O occur here.
void formatReminderPresentation(ReminderPresentation&, const ReminderNow&, ReminderWords);
}  // namespace booksync
namespace booksync {
struct ReminderListWords {
  const char* onDevice;
  const char* everyDay;
  const char* weekdays;
  const char* weekends;
  const char* day[7];
  const char* am;
  const char* pm;
  bool twelveHour;
};
void formatReminderList(ReminderList&, ReminderListWords);
}  // namespace booksync
