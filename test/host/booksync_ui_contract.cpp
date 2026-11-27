#include "booksync/interface/Library.h"
#include "booksync/ui/Home.h"

bool paintOffline(const booksync::Library& library) {
  const auto snapshot = library.snapshot();
  return library.capabilities().localReading && snapshot.catalog == booksync::Availability::NotSaved;
}

// Force the production Home operations into the UI-only link inspection.
bool paintHome(booksync::Library& library) {
  booksync::Home home(library);
  return home.snapshot().canOpenLastBook;
}

bool openHome(booksync::Library& library) {
  booksync::Home home(library);
  return home.submit() == booksync::CommandStatus::Accepted;
}
