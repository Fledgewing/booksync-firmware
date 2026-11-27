#pragma once

#include <cstdint>

namespace booksync {
// Presentation values, never wire IDs, credentials or mutable store handles.
// Durable identity and locator contracts are intentionally deferred to F08.
enum class Availability : uint8_t { NotSaved, Ready, Unsupported, Unavailable };
struct Snapshot {
  Availability catalog = Availability::NotSaved;
  uint32_t generation = 0;
  uint32_t accountEpoch = 0;
  bool canOpenLastBook = false;
};
}  // namespace booksync
