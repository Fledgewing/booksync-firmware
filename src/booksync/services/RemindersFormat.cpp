// SPDX-License-Identifier: GPL-3.0-or-later
#include "RemindersFormat.h"

#include <cstdio>
namespace booksync {
void formatReminderPresentation(ReminderPresentation& out, const ReminderNow& now, ReminderWords words) {
  std::snprintf(out.sourceLabel, sizeof(out.sourceLabel), "%s", words.onDevice);
  if (out.due.kind == ReminderKind::Interval) {
    std::snprintf(out.due.message, sizeof(out.due.message), "%s", words.eyeMessage);
    return;
  }
  if (!out.due.message[0]) std::snprintf(out.due.message, sizeof(out.due.message), "%s", words.defaultMessage);
  if (!out.due.missed) return;
  int64_t minute = (out.due.effective + int64_t(now.offsetMinutes) * 60) / 60 % 1440;
  if (minute < 0) minute += 1440;
  const unsigned hour = static_cast<unsigned>(minute / 60), part = static_cast<unsigned>(minute % 60);
  char time[24]{};
  if (words.twelveHour)
    std::snprintf(time, sizeof(time), "%u:%02u %s", hour % 12 ? hour % 12 : 12, part, hour < 12 ? words.am : words.pm);
  else
    std::snprintf(time, sizeof(time), "%02u:%02u", hour, part);
  std::snprintf(out.dueLabel, sizeof(out.dueLabel), words.dueFormat, time);
}
}  // namespace booksync
namespace booksync {
void formatReminderList(ReminderList& listValue, ReminderListWords words) {
  listValue.twelveHour = words.twelveHour;
  for (unsigned i = 0; i < listValue.count; ++i) {
    auto& rowValue = listValue.rows[i];
    std::snprintf(rowValue.sourceLabel, sizeof(rowValue.sourceLabel), "%s", words.onDevice);
    if (rowValue.fields.kind != ReminderKind::Time) continue;
    const unsigned h = rowValue.fields.minute / 60, m = rowValue.fields.minute % 60;
    if (words.twelveHour)
      std::snprintf(rowValue.timeLabel, sizeof(rowValue.timeLabel), "%u:%02u %s", h % 12 ? h % 12 : 12, m,
                    h < 12 ? words.am : words.pm);
    else
      std::snprintf(rowValue.timeLabel, sizeof(rowValue.timeLabel), "%02u:%02u", h, m);
    const auto days = rowValue.fields.days;
    if (days == 127 || days == 31 || days == 96)
      std::snprintf(rowValue.daysLabel, sizeof(rowValue.daysLabel), "%s",
                    days == 127  ? words.everyDay
                    : days == 31 ? words.weekdays
                                 : words.weekends);
    else {
      unsigned used = 0;
      rowValue.daysLabel[0] = 0;
      for (unsigned j = 0; j < 7; ++j)
        if (days & (1 << j)) {
          const int added = std::snprintf(rowValue.daysLabel + used, sizeof(rowValue.daysLabel) - used, "%s%s",
                                          used ? " " : "", words.day[j]);
          if (added < 0 || unsigned(added) >= sizeof(rowValue.daysLabel) - used) break;
          used += unsigned(added);
        }
    }
  }
}
}  // namespace booksync
