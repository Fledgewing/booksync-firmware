// SPDX-License-Identifier: GPL-3.0-or-later
// Mean-lunation semantics follow BookSync LocalDecor.kt (MIT), independently
// implemented for fixed-size firmware values. This is an estimate, not an ephemeris.
#include "Moon.h"

#include <cmath>

#include "booksync/services/Clock.h"
namespace booksync {
void projectMoonWidget(ClockStatus reading, TimeProjection options, MoonWidget& out) {
  out = {};
  if (!startTimeCard(options, out.status, sizeof(out))) return;
  const auto value = projectClock(reading.evidence, reading.utcSeconds, reading.local);
  if (!value.hasDateTime()) {
    out.status.state = TimeCardState::NotSaved;
    return;
  }
  if (value.local.year > 2100) {
    out.status.state = TimeCardState::Unsupported;
    return;
  }
  // NASA table reference: 2000-01-06 18:14 UTC. Same mean period as the
  // companion; all date arithmetic and trigonometry happen before publication.
  constexpr int64_t epoch = 947182440;
  constexpr double period = 29.530588 * 86400.0;
  constexpr double tau = 6.2831853071795864769;
  const double elapsed = double(value.utcSeconds - epoch) / period;
  const double fraction = elapsed - std::floor(elapsed);
  const unsigned next = unsigned(std::floor(fraction * 4.0)) + 1;
  out.terminator = int16_t(std::lround(std::cos(tau * fraction) * 1000.0));
  out.fraction = uint16_t(fraction * 1000.0);
  out.phase = uint8_t(unsigned(fraction * 8.0 + 0.5) % 8);
  out.illumination = uint8_t(std::lround((1.0 - std::cos(tau * fraction)) * 50.0));
  out.nextPhase = uint8_t((next % 4) * 2);
  out.nextSeconds = uint32_t((next / 4.0 - fraction) * period);
}
}  // namespace booksync
