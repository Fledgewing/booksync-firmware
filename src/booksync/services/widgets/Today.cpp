// SPDX-License-Identifier: GPL-3.0-or-later
#include "Today.h"
namespace booksync {
void projectToday(const StatsSnapshot& stats, const StatsProjection& options, StatsCard& out) {
  const bool native = stats.nativeToday.known;
  const auto& coverage = native ? stats.nativeTodayCoverage : stats.accountTodayCoverage;
  if (!startStatsCard(options, coverage, out)) return;
  out.reader = {stats.nativeToday.value / 60, stats.nativeToday.known};
  const bool accountAllowed = stats.accountTodayCoverage.scope.owner == options.provenance.owner &&
                              stats.accountTodayCoverage.scope.epoch == options.provenance.accountEpoch &&
                              options.provenance.owner != 0;
  if (accountAllowed) out.account = {stats.accountToday.value / 60, stats.accountToday.known};
  out.accountLinked = accountAllowed && stats.accountTodayCoverage.saved;
  out.value = native ? out.reader : out.account;
  // A fresh reader primary must not conceal an older visible account field.
  if (out.account.known && stats.accountTodayCoverage.through && options.now > stats.accountTodayCoverage.through) {
    const auto age = uint32_t((options.now - stats.accountTodayCoverage.through) / 86400);
    if (age > out.ageDays) {
      out.ageDays = age;
      out.savedAt = stats.accountTodayCoverage.through;
    }
    out.stale |= age > 0;
  }
  const bool empty =
      (!stats.nativeToday.known || !stats.nativeToday.value) && (!out.account.known || !stats.accountToday.value);
  out.state = !out.value.known ? StatsCardState::NotSaved : empty ? StatsCardState::Empty : StatsCardState::Ready;
}
}  // namespace booksync
