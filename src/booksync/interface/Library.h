#pragma once

#include "Capabilities.h"
#include "Commands.h"
#include "Models.h"

namespace booksync {
// UI reads bounded published values. No scans, writes, parsing or transport in
// snapshot(). submit() reports acceptance only; it never proves remote delivery.
class Library {
 public:
  virtual ~Library() = default;
  virtual Capabilities capabilities() const = 0;
  virtual Snapshot snapshot() const = 0;
  virtual CommandStatus submit(Intent intent) = 0;
};
}  // namespace booksync
