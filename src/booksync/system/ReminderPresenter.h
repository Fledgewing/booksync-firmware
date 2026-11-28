// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
class GfxRenderer;
class MappedInputManager;
namespace booksync {
// Native composition-root hooks; all are main-loop owned. No worker paints.
bool reminderLoop(GfxRenderer&, MappedInputManager&, bool page, bool visible, bool modal);
bool reminderOwnsFrame();
void reminderNavigation();
void reminderHidden(bool);
void reminderInput();
bool takeReminderSleep();
}  // namespace booksync
#ifdef SIMULATOR
#include "booksync/interface/Library.h"
namespace booksync {
uint64_t reminderSceneInputCount();
bool reminderSceneHidden();
void paintReminderScene(GfxRenderer&, Library&, unsigned state);
bool inputReminderScene(GfxRenderer&, MappedInputManager&, Library&, unsigned state);
}  // namespace booksync
#endif
