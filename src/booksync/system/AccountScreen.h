// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/Library.h"
class GfxRenderer;
class MappedInputManager;
namespace booksync {
// Explicit setup/settings navigation seam. Never invoked by an auth worker.
void showAccountForm(GfxRenderer&, MappedInputManager&, Library&);
void showServiceChoice(GfxRenderer&, MappedInputManager&, Library&);
#ifdef SIMULATOR
uint32_t serviceChoicePaints();
uint32_t accountFormPaints();
#endif
}  // namespace booksync
