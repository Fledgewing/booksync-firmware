// SPDX-License-Identifier: GPL-3.0-or-later
#include "Records.h"
namespace booksync {
namespace {
bool accountField(const ReadingCoverage& coverage, const StatsProjection& options) {
  return coverage.saved && coverage.origin == ReadingOrigin::Account && options.provenance.owner &&
         coverage.scope.owner == options.provenance.owner && coverage.scope.epoch == options.provenance.accountEpoch;
}
StatsCardSource metricOrigin(const ReadingCoverage& coverage) {
  return coverage.origin == ReadingOrigin::Account      ? StatsCardSource::Account
         : coverage.origin == ReadingOrigin::ThisReader ? StatsCardSource::Reader
                                                        : StatsCardSource::Unknown;
}
}  // namespace
void projectRecords(const StatsSnapshot& stats, const StatsProjection& options, StatsCard& out, StatsRecords& records) {
  for (auto& value : records.values) value = {};
  records.sources.fill(StatsCardSource::Unknown);
  const bool books = stats.finished.known && accountField(stats.finishedCoverage, options);
  const bool best = stats.bestStreak.known && (stats.streakCoverage.origin != ReadingOrigin::Account ||
                                               accountField(stats.streakCoverage, options));
  const bool native = stats.recordedPages.known || stats.longestSession.known || stats.bestDayPages.known;
  const auto& coverage = native ? stats.recordsCoverage : books ? stats.finishedCoverage : stats.streakCoverage;
  if (!startStatsCard(options, coverage, out)) return;
  records.values[0] = statsMetric(stats.recordedPages);
  records.values[1] = statsMetric(stats.longestSession);
  records.values[2] = statsMetric(stats.bestDayPages);
  for (unsigned i = 0; i < 3; ++i)
    if (records.values[i].known) records.sources[i] = metricOrigin(stats.recordsCoverage);
  if (books) {
    records.values[3] = statsMetric(stats.finished);
    records.sources[3] = StatsCardSource::Account;
  }
  if (best) {
    records.values[4] = statsMetric(stats.bestStreak);
    records.sources[4] = metricOrigin(stats.streakCoverage);
  }
  bool any = false, all = true, positive = false;
  for (unsigned i = 0; i < 5; ++i) {
    const auto value = records.values[i];
    all &= value.known;
    any |= value.known;
    positive |= value.known && value.value != 0;
    if (!out.value.known && value.known) out.value = value;
    if (value.known && records.sources[i] == StatsCardSource::Account) out.accountLinked = true;
  }
  // One conservative age identifies the oldest visible saved account field.
  auto age = [&](const ReadingCoverage& field) {
    if (!options.now || !field.through || field.through >= options.now) return;
    const auto days = uint32_t((options.now - field.through) / 86400);
    if (days > out.ageDays) {
      out.ageDays = days;
      out.savedAt = field.through;
    }
    out.stale |= days > 0;
  };
  if (books) age(stats.finishedCoverage);
  if (best && records.sources[4] == StatsCardSource::Account) age(stats.streakCoverage);
  out.hours = stats.longestSession.value / 3600;
  out.minutes = uint8_t(stats.longestSession.value / 60 % 60);
  out.state = !any ? StatsCardState::NotSaved : all && !positive ? StatsCardState::Empty : StatsCardState::Ready;
}
}  // namespace booksync
