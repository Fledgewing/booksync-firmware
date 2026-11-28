// SPDX-License-Identifier: GPL-3.0-or-later
// Semantics mirror BookSync companion ReadingStats.kt; independently implemented for bounded page cursors.
#include "ReadingStats.h"

#include <Logging.h>

#include <algorithm>
namespace booksync {
StatsSource PageStatsSource::freeze(ReadingScope scope, uint64_t since, uint64_t through, bool complete) {
  row_ = 0;
  count_ = journal_.count();
  return {journal_.available() ? next : nullptr, this, scope, since, through, complete && !journal_.capped()};
}
StatsStep PageStatsSource::next(void* context, storage::PageEvent& out) {
  auto& self = *static_cast<PageStatsSource*>(context);
  if (self.row_ == self.count_) return StatsStep::End;
  if (self.journal_.read(self.row_, out) != storage::JournalResult::Ok) return StatsStep::Error;
  ++self.row_;
  return StatsStep::Record;
}
bool ReadingStats::split(uint64_t t, StatsDaySlice& slice) const {
  if (calendar_.split) return calendar_.split(calendar_.context, t, slice) && slice.end > t;
  const int64_t shifted = int64_t(t) + int64_t(offset_) * 60;
  slice.day = shifted / 86400 - (shifted < 0 && shifted % 86400 != 0);
  slice.end = uint64_t((slice.day + 1) * 86400 - int64_t(offset_) * 60);
  return slice.end > t;
}
bool ReadingStats::begin(StatsSource source, uint64_t now, int16_t offset, ReadingValue watermark,
                         StatsCalendar calendar) {
  busy_ = false;
  published_ = {};  // Never reuse a prior account's successful history after failure.
  pending_ = {};
  if (!source.next || !now || now > uint64_t(INT64_MAX - 172800) || offset < -1440 || offset > 1440) {
    LOG_ERR("BSRS", "Stats source or calendar unavailable");
    return false;
  }
  now_ = now;
  offset_ = offset;
  calendar_ = calendar;
  source_ = source;
  StatsDaySlice today;
  if (!split(now, today)) {
    LOG_ERR("BSRS", "Stats date unavailable");
    return false;
  }
  pending_.firstDay = today.day - Days + 1;
  pending_.coverage = {ReadingOrigin::ThisReader,
                       source.scope,
                       source.since,
                       source.through,
                       true,
                       source.complete && source.since > 0 && source.since <= now && source.through >= now};
  pending_.includedThrough = watermark;
  pending_.recordedPages = {0, true};
  pending_.bestDayPages = {0, true};
  pending_.longestSession = {0, true};
  pending_.bestStreak = {0, true};
  sinceMidnight_ = false;
  pending_.pendingToday = {0, pending_.coverage.complete && watermark.known && watermark.value <= now};
  // Known empty days require capture coverage from midnight, never mere absence
  // of rows. The current partial day is known once today's capture began.
  if (pending_.coverage.complete) {
    StatsDaySlice start;
    if (!split(source.since, start)) {
      LOG_ERR("BSRS", "Capture coverage date unavailable");
      return false;
    }
    sinceDay_ = start.day;
    StatsDaySlice before;
    sinceMidnight_ = split(source.since - 1, before) && before.day != start.day;
    for (unsigned i = 0; i < Days; ++i) {
      const auto day = pending_.firstDay + i;
      pending_.days[i].known = day > start.day || (day == start.day && (sinceMidnight_ || day == today.day));
    }
  }
  pending_.todayPages = {0, pending_.days.back().known};
  pageRunCount_ = 0;
  pageRunDay_ = INT64_MIN;
  lastStart_ = unionEnd_ = sessionEnd_ = sessionSeconds_ = pendingEnd_ = readRun_ = 0;
  lastReadDay_ = INT64_MIN;
  busy_ = true;
  return true;
}
void ReadingStats::markIncomplete() {
  pending_.coverage.complete = false;
  pending_.pendingToday.known = false;
  if (!pending_.todayPages.value) pending_.todayPages.known = false;
  // A gap revokes the evidence behind empty buckets; observed partial time
  // remains usable with incomplete coverage, but absence cannot become zero.
  for (auto& day : pending_.days)
    if (!day.value) day.known = false;
}
bool ReadingStats::tick(uint16_t allowance) {
  if (!allowance || allowance > Batch) return false;
  if (!busy_) return true;
  for (unsigned i = 0; i < allowance; ++i) {
    const auto step = source_.next(source_.context, event_);
    if (step == StatsStep::Error) {
      LOG_ERR("BSRS", "Stats cursor failed; partial result discarded");
      busy_ = false;
      return false;
    }
    if (step == StatsStep::End) {
      pending_.today = pending_.days.back();
      const auto today = pending_.firstDay + Days - 1;
      const bool active = lastReadDay_ == today || lastReadDay_ == today - 1;
      pending_.streak = {active ? readRun_ : 0, false};
      if (pending_.coverage.complete) {
        // An observed run touching the start of coverage may continue into
        // unknown older history. A fully captured empty preceding day proves
        // its actual start; no 365-day display bound truncates the counters.
        const auto preceding = active ? runStartDay_ - 1 : today - 1;
        pending_.streak.known = preceding > sinceDay_ || (preceding == sinceDay_ && sinceMidnight_);
      }
      published_ = pending_;
      busy_ = false;
      return true;
    }
    const auto& e = event_;
    if (e.flags & storage::PageEvent::Gap) {
      markIncomplete();
      continue;
    }
    if (!e.start || !e.seconds || e.start >= now_) continue;
    if (e.start < lastStart_) {
      markIncomplete();
      continue;
    }
    lastStart_ = e.start;
    const auto end = e.start + std::min<uint64_t>({e.seconds, 86400, now_ - e.start});
    if (e.start > sessionEnd_ && e.start - sessionEnd_ > 300) sessionSeconds_ = 0;
    sessionSeconds_ += end > std::max(e.start, sessionEnd_) ? end - std::max(e.start, sessionEnd_) : 0;
    sessionEnd_ = std::max(sessionEnd_, end);
    pending_.longestSession.value = std::max(pending_.longestSession.value, sessionSeconds_);
    ++pending_.recordedPages.value;
    // A timed visit belongs to its START day, even when time spans midnight.
    // Revisited/overlapping pages remain visits; only elapsed time is unioned.
    StatsDaySlice visitDay;
    if (!split(e.start, visitDay)) {
      LOG_ERR("BSRS", "Page visit date unavailable; partial result discarded");
      busy_ = false;
      return false;
    }
    if (visitDay.day != pageRunDay_) {
      pageRunDay_ = visitDay.day;
      pageRunCount_ = 0;
    }
    ++pageRunCount_;
    pending_.bestDayPages.value = std::max(pending_.bestDayPages.value, pageRunCount_);
    if (visitDay.day == pending_.firstDay + Days - 1) {
      ++pending_.todayPages.value;
      pending_.todayPages.known = true;
    }
    // Union timed visits, excluding idle gaps; neither native cumulative totals
    // nor session summaries can enter this sum a second time.
    uint64_t t = std::max(e.start, unionEnd_);
    while (t < end) {
      StatsDaySlice slice;
      if (!split(t, slice)) {
        LOG_ERR("BSRS", "Stats calendar failed; partial result discarded");
        busy_ = false;
        return false;
      }
      const auto stop = std::min(end, slice.end);
      if (slice.day != lastReadDay_) {
        if (lastReadDay_ != INT64_MIN && slice.day == lastReadDay_ + 1)
          ++readRun_;
        else {
          readRun_ = 1;
          runStartDay_ = slice.day;
        }
        lastReadDay_ = slice.day;
        pending_.bestStreak.value = std::max(pending_.bestStreak.value, readRun_);
      }
      const auto index = slice.day - pending_.firstDay;
      if (index >= 0 && index < Days) {
        auto& day = pending_.days[size_t(index)];
        day.value += stop - t;
        day.known = true;
      }
      t = stop;
    }
    unionEnd_ = std::max(unionEnd_, end);
    // Pending uses its OWN union: an included visit may overlap a newer visit.
    // Only strictly newer starts qualify, exactly as the companion watermark.
    if (pending_.pendingToday.known && e.start > pending_.includedThrough.value) {
      t = std::max(e.start, pendingEnd_);
      while (t < end) {
        StatsDaySlice slice;
        if (!split(t, slice)) {
          LOG_ERR("BSRS", "Pending stats calendar failed");
          busy_ = false;
          return false;
        }
        const auto stop = std::min(end, slice.end);
        if (slice.day == pending_.firstDay + Days - 1) pending_.pendingToday.value += stop - t;
        t = stop;
      }
      pendingEnd_ = std::max(pendingEnd_, end);
    }
  }
  return true;
}
void ReadingStats::combine(const RemoteReadingStats* remote, ReadingScope current, bool offline, int64_t today,
                           StatsSnapshot& out) const {
  // Aggregate assignment materializes a >6 KiB temporary with the ESP32
  // compiler. Reset bounded members directly; output is caller-owned storage.
  out.days.fill({});
  out.today = out.localToday = out.streak = out.bestStreak = out.target = out.year = out.finished =
      out.includedThrough = out.recordedPages = out.longestSession = out.nativeToday = out.accountToday =
          out.todayPages = out.bestDayPages = {};
  out.daysCoverage = out.streakCoverage = out.goalCoverage = out.finishedCoverage = out.recordsCoverage =
      out.localTodayCoverage = out.nativeTodayCoverage = out.accountTodayCoverage = {};
  out.todayMerge = TodayMerge::LocalOnly;
  out.offline = offline;
  out.firstDay = today - Days + 1;
  const bool localAllowed =
      published_.coverage.saved && (published_.coverage.scope.owner == 0 || published_.coverage.scope == current);
  if (localAllowed) {
    for (unsigned i = 0; i < Days; ++i) {
      const auto n = published_.firstDay + i - out.firstDay;
      if (n >= 0 && n < Days) out.days[size_t(n)] = published_.days[i];
    }
    out.today = out.nativeToday = out.days.back();
    out.nativeTodayCoverage = published_.coverage;
    out.todayPages = published_.firstDay + Days - 1 == today ? published_.todayPages : ReadingValue{};
    out.bestDayPages = published_.bestDayPages;
    out.daysCoverage = out.recordsCoverage = published_.coverage;
    out.recordedPages = published_.recordedPages;
    out.longestSession = published_.longestSession;
    out.streak = published_.streak;
    out.bestStreak = published_.bestStreak;
    out.streakCoverage = published_.coverage;
  }
  if (!remote || remote->scope != current || !current.owner) return;
  const ReadingCoverage account{ReadingOrigin::Account, current, 0, remote->snapshotAt, true, remote->complete};
  if (remote->daysAvailable && remote->dayCount <= Days) {
    out.days.fill({});
    // A server-only snapshot supplies its own calendar window; once local has
    // a date, align by date rather than by array position.
    for (unsigned i = 0; i < remote->dayCount; ++i) {
      const auto n = remote->firstDay + i - out.firstDay;
      if (n >= 0 && n < Days) out.days[size_t(n)] = remote->days[i];
    }
    out.today = out.accountToday = out.days.back();
    out.accountTodayCoverage = account;
    out.daysCoverage = account;
    const bool reconciled =
        localAllowed && published_.firstDay + Days - 1 == today && published_.coverage.scope == current &&
        published_.coverage.complete && remote->includedThrough.known && remote->includedThrough.value <= now_ &&
        published_.includedThrough.known && published_.includedThrough.value == remote->includedThrough.value &&
        published_.pendingToday.known && out.today.known;
    if (reconciled && out.today.value <= UINT64_MAX - published_.pendingToday.value) {
      out.today.value += published_.pendingToday.value;
      out.days.back() = out.today;
      out.todayMerge = TodayMerge::Watermarked;
      out.includedThrough = remote->includedThrough;
    } else {
      out.todayMerge = TodayMerge::Separate;
      if (localAllowed && published_.firstDay + Days - 1 == today) {
        out.localToday = published_.today;
        out.localTodayCoverage = published_.coverage;
      }
    }
  }
  if (remote->streak.known) {
    out.streak = remote->streak;
    out.bestStreak = remote->bestStreak;
    out.streakCoverage = account;
  }
  if (remote->goalAvailable) {
    out.target = remote->target;
    out.year = remote->year;
    out.goalCoverage = account;
  }
  if (remote->finished.known) {
    out.finished = remote->finished;
    out.finishedCoverage = account;
  }
}
}  // namespace booksync
