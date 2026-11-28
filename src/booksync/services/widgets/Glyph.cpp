// SPDX-License-Identifier: GPL-3.0-or-later
// Daily Latin selection follows BookSync companion LocalDecor.kt (MIT).
#include "Glyph.h"
#include "booksync/services/Clock.h"
namespace booksync {
uint32_t glyphCodepoint(ClockStatus reading) {
  const auto clock = projectClock(reading.evidence, reading.utcSeconds, reading.local);
  if (!clock.hasDateTime() || clock.local.year > 2100) return 0;
  const int64_t day = (clock.utcSeconds + clock.utcOffsetSeconds) / 86400;
  return 65 + static_cast<uint32_t>((day % 26 + 26) % 26);
}
void projectGlyphWidget(ClockStatus reading, bool faceSupported, TimeProjection options, GlyphWidget& out) {
  out = {};
  if (!startTimeCard(options, out.status, sizeof(out))) return;
  const auto clock = projectClock(reading.evidence, reading.utcSeconds, reading.local);
  if (!clock.hasDateTime()) {
    out.status.state = TimeCardState::NotSaved; out.issue = GlyphIssue::ClockUnavailable; return;
  }
  if (clock.local.year > 2100) {
    out.status.state = TimeCardState::Unsupported; out.issue = GlyphIssue::DateUnsupported; return;
  }
  if (!faceSupported) {
    out.status.state = TimeCardState::Unsupported; out.issue = GlyphIssue::FaceUnavailable; return;
  }
  out.codepoint = glyphCodepoint(clock);
}
}  // namespace booksync
