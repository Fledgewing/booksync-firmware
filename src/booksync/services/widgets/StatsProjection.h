// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/StatsWidgets.h"
#include "booksync/services/ReadingStats.h"
namespace booksync {
struct StatsProjection {
  Provenance provenance{};
  uint64_t savedAt = 0;
  uint32_t ageDays = 0;
  StatsCardState availability = StatsCardState::Ready;
  bool enabled = true, stale = false;
};
inline bool startStatsCard(const StatsProjection& options, const ReadingCoverage& coverage, StatsCard& out) {
  out.clear();
  out.provenance = options.provenance;
  out.savedAt = options.savedAt;
  out.ageDays = options.ageDays;
  out.stale = options.stale;
  out.complete = coverage.complete;
  out.source = coverage.origin == ReadingOrigin::Account      ? StatsCardSource::Account
               : coverage.origin == ReadingOrigin::ThisReader ? StatsCardSource::Reader
                                                              : StatsCardSource::Unknown;
  out.state = options.availability;
  if (!options.enabled) out.state = StatsCardState::Off;
  if (coverage.origin == ReadingOrigin::Account &&
      (coverage.scope.owner != options.provenance.owner || coverage.scope.epoch != options.provenance.accountEpoch))
    out.state = StatsCardState::NotSaved;
  return out.state == StatsCardState::Ready;
}
inline void statsFreshness(StatsProjection& options, const ReadingCoverage& coverage, uint64_t now) {
  options.savedAt = coverage.through;
  // Zero is an absent timestamp, not evidence of a save at the Unix epoch.
  options.ageDays = coverage.through && now > coverage.through ? uint32_t((now - coverage.through) / 86400) : 0;
  options.stale = coverage.origin == ReadingOrigin::Account && options.ageDays > 0;
}
inline StatsMetric statsMetric(ReadingValue v) { return {v.value, v.known}; }
}  // namespace booksync
