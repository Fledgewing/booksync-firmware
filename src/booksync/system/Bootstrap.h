#pragma once

#include "Upgrade.h"
#include "activities/ActivityManager.h"
#include "booksync/interface/Library.h"

namespace booksync {
// Neutral retained-runtime bridge. Main loop only; uses ActivityManager ownership.
Library& homeLibrary();
void showHomeSettings(GfxRenderer& renderer, MappedInputManager& input);
std::unique_ptr<Activity> createHomeEditor(GfxRenderer& renderer, MappedInputManager& input);
void showHome(GfxRenderer& renderer, MappedInputManager& input, HalDisplay::RefreshMode refresh);
#ifdef SIMULATOR
// Scenario-only counter; caller holds RenderLock.
uint32_t homeScenePaints();
void showHomeScene(GfxRenderer& renderer, MappedInputManager& input, Library& library, bool edit = false);
#endif
}  // namespace booksync
