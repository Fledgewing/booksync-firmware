#pragma once

namespace booksync {
// Capability presence does not imply connectivity or permission.
struct Capabilities {
  bool localReading = true;
  bool catalog = false;
  bool delivery = false;
};
}  // namespace booksync
