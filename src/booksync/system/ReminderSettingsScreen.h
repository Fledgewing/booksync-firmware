// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
class GfxRenderer;
class MappedInputManager;
namespace booksync {
void showReminderSettings(GfxRenderer&, MappedInputManager&);
}

#ifdef SIMULATOR
#include "booksync/interface/Library.h"
namespace booksync {
void showReminderSettingsScene(GfxRenderer&, MappedInputManager&, Library&);
void prepareReminderSettingsScene(unsigned state);
bool inputReminderSettingsScene(unsigned which);
}  // namespace booksync
#endif
