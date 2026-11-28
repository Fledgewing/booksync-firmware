// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/LayoutEdit.h"
namespace booksync {
// Pure bounded preview; publication independently validates all stored instances.
LayoutPreview previewGeometry(const HomePage& page, uint8_t index, LayoutStep step);
bool supportedGeometry(WidgetKind kind, uint8_t w, uint8_t h);
}  // namespace booksync
