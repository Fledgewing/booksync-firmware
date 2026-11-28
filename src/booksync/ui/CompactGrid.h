// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/ui/WidgetHost.h"
namespace booksync {
// Approved compact tokens are minimum margins. Calibrated bezel insets can
// enlarge them; the compact footer owns the existing hint band exactly once.
struct CompactGrid {
  static constexpr int HeaderHeight = 64, FooterHeight = 42, Margin = 12, Gap = 8;
  int gridLeft = 0, gridWidth = 0, gridTop = 0, gridEdge = 0;
  uint8_t cols = 2, rows = 6;
  static CompactGrid forHomePanel(int panelWidth, int panelHeight, int topInset, int rightInset, int bottomInset,
                                  int leftInset, uint8_t cols = 2, uint8_t rows = 6) {
    const int left = leftInset > Margin ? leftInset : Margin;
    const int right = rightInset > Margin ? rightInset : Margin;
    const int top = topInset > HeaderHeight + 6 ? topInset : HeaderHeight + 6;
    const int footer = bottomInset > FooterHeight ? bottomInset : FooterHeight;
    return {left, panelWidth - left - right, top, panelHeight - footer, cols, rows};
  }
  WidgetCell compactTile(WidgetPlacement placement) const {
    if (!cols || !rows || gridWidth <= 0 || gridEdge <= gridTop || !placement.w || !placement.h ||
        placement.x + placement.w > cols || placement.y + placement.h > rows)
      return {};
    const int stepX = (gridWidth + Gap) / cols, stepY = (gridEdge - gridTop + Gap) / rows;
    return {gridLeft + stepX * placement.x,
            gridTop + stepY * placement.y,
            stepX * placement.w - Gap,
            stepY * placement.h - Gap,
            false,
            false};
  }
};
}  // namespace booksync
