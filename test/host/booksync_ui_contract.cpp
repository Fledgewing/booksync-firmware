#include "booksync/interface/Library.h"

bool paintOffline(const booksync::Library& library) {
  const auto snapshot = library.snapshot();
  return library.capabilities().localReading && snapshot.catalog == booksync::Availability::NotSaved;
}
