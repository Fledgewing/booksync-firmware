// SPDX-License-Identifier: GPL-3.0-or-later
#include "Forecast.h"

#include <chrono>
namespace booksync {
void projectForecast(const ReadingForecast& source, const StatsProjection& options, StatsCard& out,
                     StatsForecast& detail) {
  detail = {};
  if (!startStatsCard(options, source.card.timeLeftCoverage, out)) return;
  const auto& key = source.current.key;
  const auto scoped = [&](const ReadingCoverage& coverage) {
    return coverage.scope.owner == 0 || (coverage.scope.owner == options.provenance.owner &&
                                         coverage.scope.epoch == options.provenance.accountEpoch);
  };
  if (!source.card.progressCoverage.saved || source.card.progressCoverage.origin != ReadingOrigin::ThisReader ||
      !scoped(source.card.progressCoverage) || !scoped(source.card.timeLeftCoverage) || !source.current.identityKnown ||
      !(key == source.observed) ||
      (key.provenance.owner && (key.provenance.owner != options.provenance.owner ||
                                key.provenance.accountEpoch != options.provenance.accountEpoch)) ||
      !source.matchingEdition || !source.card.progress.known || source.card.progress.value > 100) {
    out.state = StatsCardState::NotSaved;
    return;
  }
  out.accountLinked = key.provenance.owner != 0;
  detail.title = source.current.title;
  detail.title.back() = 0;
  if (source.card.progress.value == 100) {
    out.value = {0, true};
    out.state = StatsCardState::Empty;
    detail.finished = true;
    return;
  }
  if (!source.card.timeLeftCoverage.saved || source.card.timeLeftCoverage.origin != ReadingOrigin::ThisReader ||
      source.measuredVisits < 5 || !source.card.timeLeft.known || !source.card.timeLeft.value) {
    out.state = StatsCardState::NotSaved;
    return;
  }
  if (source.card.timeLeft.value > 3153600000ull) {
    out.state = StatsCardState::Oversize;
    return;
  }
  out.value = statsMetric(source.card.timeLeft);
  // Duration rounds up as a whole before splitting, including 59m59s -> 1h00m.
  const auto minutes = (out.value.value + 59) / 60;
  out.hours = minutes / 60;
  out.minutes = uint8_t(minutes % 60);
  if (source.card.finishDay.known && source.card.finishDayCoverage.saved && scoped(source.card.finishDayCoverage) &&
      options.today > 0 && source.card.finishDay.value >= uint64_t(options.today) &&
      source.card.finishDay.value - uint64_t(options.today) <= 36500) {
    using namespace std::chrono;
    const year_month_day date{sys_days{days{int64_t(source.card.finishDay.value)}}};
    if (date.ok() && int(date.year()) >= 1970 && int(date.year()) <= 9999) {
      detail.year = uint16_t(int(date.year()));
      detail.month = uint8_t(unsigned(date.month()));
      detail.day = uint8_t(unsigned(date.day()));
      detail.weekday = uint8_t(weekday{sys_days{date}}.c_encoding());
      detail.dateKnown = true;
    }
  }
  out.state = StatsCardState::Ready;
}
}  // namespace booksync
