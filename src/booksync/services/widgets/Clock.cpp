// SPDX-License-Identifier: GPL-3.0-or-later
#include "Clock.h"

#include "booksync/services/Clock.h"
namespace booksync {
void projectClockWidget(ClockStatus reading, TimeProjection options, ClockWidget& out) {
  out = {};
  if (!startTimeCard(options, out.status, sizeof(out))) return;
  // Validate borrowed source values again at the projection boundary. A known
  // provenance without an actual date never produces a zero-filled clock face.
  const auto value = projectClock(reading.evidence, reading.utcSeconds, reading.local);
  if (!value.hasDateTime()) {
    out.status.state = TimeCardState::NotSaved;
    return;
  }
  out.reading = value;
  out.twelveHour = options.twelveHour;
}
}  // namespace booksync
