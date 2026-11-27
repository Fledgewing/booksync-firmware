#pragma once

#include "booksync/interface/Library.h"

namespace booksync {
// Temporary worker-side factory; no adapter or network is needed to boot.
Library& offlineLibrary();
}  // namespace booksync
