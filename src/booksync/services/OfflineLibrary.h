#pragma once

#include "booksync/interface/Library.h"

namespace booksync {
// Device-local facade; no adapter or network is needed to boot.
Library& offlineLibrary();
// Main-loop-only recent-book shell handoff, alongside F13 indexed open intents.
void publishHomeBook(bool available, const char* title = nullptr);
bool takeHomeOpenRequest();
// Main-loop publication; the worker snapshot has already been normalized.
void publishAccountView(AccountView value);
void publishClockStatus(ClockStatus value);
}  // namespace booksync
