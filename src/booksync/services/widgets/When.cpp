// SPDX-License-Identifier: GPL-3.0-or-later
#include "When.h"
namespace booksync {
void projectWhen(const StatsSnapshot& stats, const StatsProjection& options, StatsCard& out, StatsHours& hours) {
  for (auto& hour : hours.totals) hour = {};
  hours.peakHour = hours.after18 = 0;
  hours.peakKnown = false;
  if (!startStatsCard(options, stats.hoursCoverage, out)) return;
  if (!stats.hoursAvailable) {
    out.state = stats.recordsCoverage.saved ? StatsCardState::Unsupported : StatsCardState::NotSaved;
    return;
  }
  uint64_t peak = 0, after = 0;
  for (unsigned i = 0; i < 24; ++i) {
    if (!(stats.hoursKnown & (1u << i))) continue;
    const auto value = stats.hours[i];
    hours.totals[i] = {value, true};
    if (value > UINT64_MAX - out.value.value) {
      out.state = StatsCardState::Oversize;
      return;
    }
    out.value.value += value;
    out.value.known = true;
    if (value > peak) {
      peak = value;
      hours.peakHour = i;
    }
    if (i >= 18) after += value;  // This subset cannot exceed the already checked total.
  }
  if (out.value.value > UINT64_MAX / 100) {
    out.state = StatsCardState::Oversize;
    return;
  }
  hours.peakKnown = peak != 0;
  if (peak) hours.after18 = uint8_t(after * 100 / out.value.value);
  const bool emptyHistory = stats.recordedPages.known && stats.recordedPages.value == 0;
  if (!out.value.known && emptyHistory) out.value = {0, true};
  out.state = !out.value.known   ? StatsCardState::NotSaved
              : !out.value.value ? StatsCardState::Empty
                                 : StatsCardState::Ready;
}
}  // namespace booksync
