#pragma once
#include "booksync/interface/Reminders.h"

namespace booksync {
// Capability presence does not imply connectivity or permission.
struct Capabilities {
  bool localReading = true;
  bool catalog = false;
  bool delivery = false;
  ReminderCapabilities reminders{};
};
}  // namespace booksync
