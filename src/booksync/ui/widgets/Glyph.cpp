// SPDX-License-Identifier: GPL-3.0-or-later
#include "booksync/ui/widgets/Glyph.h"
namespace booksync::widgets {
bool Glyph::layoutTime(const GlyphWidget& glyphValue, WidgetBox box, TimeDrawing& out) {
  auto status = glyphValue.status;
  status.state = boundedGlyphCard(glyphValue);
  if (!timeFrame(status, sizeof(glyphValue), box, out)) return false;
  if (status.state != TimeCardState::Ready) return true;
  out.glyph = {10,27,50,57};
  timeLine(out, TimeLine::GlyphCode, 70,31,box.width-80,18);
  timeLine(out, TimeLine::GlyphName, 70,53,box.width-80,18);
  timeLine(out, glyphValue.status.stale ? TimeLine::Stale : TimeLine::GlyphSource, 10,box.height-22,box.width-20,16);
  return true;
}
}  // namespace booksync::widgets
