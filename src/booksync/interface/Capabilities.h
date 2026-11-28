#pragma once

namespace booksync {
// Capability presence does not imply connectivity or permission.
struct Capabilities {
  bool localReading = true;
  bool catalog = false;
  bool delivery = false;
  bool reminders = false;
};
}  // namespace booksync
