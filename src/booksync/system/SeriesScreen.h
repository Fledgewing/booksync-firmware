// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/Library.h"
class GfxRenderer;
class MappedInputManager;
namespace booksync {
// Explicit existing Library/reader caller seam. Push/pop preserves its context.
void showSeries(GfxRenderer&, MappedInputManager&, Library&, const char* key, BookKey current = {},
                bool currentKnown = false);
// Shared approved grouped-Series preview header; callers own existing navigation.
void drawSeriesGroupHeader(GfxRenderer&, int x, int y, int width, const char* title, uint32_t total);
#ifdef SIMULATOR
uint32_t seriesPaints();
bool seriesScenarioInvariant(); // Caller holds RenderLock after a completed paint.
void seriesScenarioBegin(bool count = false, const char* typed = nullptr);
#endif
}  // namespace booksync
