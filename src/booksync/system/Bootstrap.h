#pragma once

#include "activities/ActivityManager.h"
#include "booksync/interface/Library.h"

namespace booksync {
// Neutral retained-runtime bridge. Main loop only; uses ActivityManager ownership.
Library& homeLibrary();
void showHome(GfxRenderer& renderer, MappedInputManager& input, HalDisplay::RefreshMode refresh);
}  // namespace booksync
