// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
class GfxRenderer;
namespace booksync {
// Bounded native paragraph layout. `draw=false` measures the identical lines.
void paintReminderParagraph(GfxRenderer&, const char*, int x, int& y, int width, bool draw = true);
}  // namespace booksync
