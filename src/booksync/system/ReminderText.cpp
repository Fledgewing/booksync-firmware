// SPDX-License-Identifier: GPL-3.0-or-later
#include "ReminderText.h"

#include <GfxRenderer.h>

#include <cstring>

#include "fontIds.h"
namespace booksync {
void paintReminderParagraph(GfxRenderer& renderer, const char* text, int x, int& y, int width, bool draw) {
  // One maximum-size message line; no vectors/strings or per-paint heap churn.
  // Stream longer translated help text through the same bounded scratch.
  char line[241]{};
  while (*text) {
    const char* cursor = text;
    const char* word = nullptr;
    unsigned used = 0, wordEnd = 0;
    while (*cursor) {
      const auto c = static_cast<unsigned char>(*cursor);
      const unsigned n = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
      if (used + n >= sizeof(line)) break;
      std::memcpy(line + used, cursor, n);
      line[used + n] = 0;
      if (used && renderer.getTextWidth(UI_10_FONT_ID, line) > width) {
        line[used] = 0;
        break;
      }
      if (c == ' ') {
        word = cursor + n;
        wordEnd = used;
      }
      used += n;
      cursor += n;
    }
    if (*cursor && word && wordEnd) {
      line[wordEnd] = 0;
      text = word;
    } else {
      text = cursor;
    }
    if (draw) renderer.drawText(UI_10_FONT_ID, x, y, line);
    y += renderer.getLineHeight(UI_10_FONT_ID) + 4;
  }
}
}  // namespace booksync
