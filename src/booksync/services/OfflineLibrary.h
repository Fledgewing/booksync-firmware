#pragma once

#include "booksync/interface/Library.h"

namespace booksync {
// Temporary worker-side factory; no adapter or network is needed to boot.
Library& offlineLibrary();
// Main-loop-only shell handoff; F13 will publish indexed local books.
void publishHomeBook(bool available);
bool takeHomeOpenRequest();
}  // namespace booksync
