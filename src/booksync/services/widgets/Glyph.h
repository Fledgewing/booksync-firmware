// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TimeProjection.h"
namespace booksync {
uint32_t glyphCodepoint(ClockStatus);
void projectGlyphWidget(ClockStatus, bool faceSupported, TimeProjection, GlyphWidget&);
}  // namespace booksync
