// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "activities/Activity.h"
#include "booksync/interface/Library.h"
namespace booksync {
std::unique_ptr<Activity> createSourceListScreen(GfxRenderer&, MappedInputManager&, Library&, WindowSource);
}
