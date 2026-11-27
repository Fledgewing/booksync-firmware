#pragma once

#include "booksync/interface/Library.h"

namespace booksync {
// Device-local facade; no adapter or network is needed to boot.
Library& offlineLibrary();
// Main-loop-only recent-book shell handoff, alongside F13 indexed open intents.
void publishHomeBook(bool available, const char* title = nullptr);
bool takeHomeOpenRequest();
}  // namespace booksync
