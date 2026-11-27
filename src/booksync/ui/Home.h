#pragma once

#include "booksync/interface/Library.h"

namespace booksync {
// The F04 shell has one bounded local action. Widget layout belongs to F34.
class Home {
  Library& library;

 public:
  explicit Home(Library& library) : library(library) {}
  Snapshot snapshot() const { return library.snapshot(); }
  CommandStatus submit() { return library.submit(Intent::OpenLastBook); }
};
}  // namespace booksync
