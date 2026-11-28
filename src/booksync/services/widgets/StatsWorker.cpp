// SPDX-License-Identifier: GPL-3.0-or-later
#include "StatsWorker.h"

#include <Logging.h>

#include <ctime>
namespace booksync {
namespace {
int64_t civilDay(const tm& date) {
  int y = date.tm_year + 1900;
  const unsigned m = date.tm_mon + 1;
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yo = unsigned(y - era * 400);
  const unsigned d = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + date.tm_mday - 1;
  return int64_t(era) * 146097 + yo * 365 + yo / 4 - yo / 100 + d - 719468;
}
}  // namespace
bool StatsWorker::split(void*, uint64_t t, StatsDaySlice& out) {
  if (t > uint64_t(INT64_MAX)) return false;
  const time_t timestamp = time_t(t);
  tm date{};
  if (!localtime_r(&timestamp, &date)) return false;
  out.day = civilDay(date);
  date.tm_hour = date.tm_min = date.tm_sec = 0;
  ++date.tm_mday;
  date.tm_isdst = -1;
  const auto end = mktime(&date);
  if (end <= timestamp) return false;
  out.end = uint64_t(end);
  return true;
}
bool StatsWorker::splitHour(void*, uint64_t stamp, StatsHourSlice& out) {
  if (stamp > uint64_t(INT64_MAX - 3600)) return false;
  const time_t instant = time_t(stamp);
  tm date{};
  if (!localtime_r(&instant, &date) || date.tm_min < 0 || date.tm_min > 59 || date.tm_sec < 0 || date.tm_sec > 59)
    return false;
  auto sameHour = [&](uint64_t sample) {
    const time_t value = time_t(sample);
    tm mapped{};
    return localtime_r(&value, &mapped) && mapped.tm_year == date.tm_year && mapped.tm_yday == date.tm_yday &&
           mapped.tm_hour == date.tm_hour;
  };
  uint64_t end = stamp + 3600 - unsigned(date.tm_min) * 60 - unsigned(date.tm_sec);
  // UTC candidates avoid mktime's ambiguous repeated-hour choice. An unusual
  // in-hour zone transition is located only when the ordinary boundary fails.
  if (!sameHour(end - 1)) {
    uint64_t low = stamp + 1, high = end - 1;
    while (low < high) {
      const auto mid = low + (high - low) / 2;
      if (sameHour(mid))
        low = mid + 1;
      else
        high = mid;
    }
    end = low;
  }
  out = {end, uint8_t(date.tm_hour)};
  return end > stamp;
}
bool StatsWorker::begin(uint16_t requested, Provenance provenance, bool clockKnown, ServerAdapter* adapter) {
  busy_ = false;
  published_.requested = requested;
  for (auto& card : published_.cards) card.clear();
  options_.provenance = provenance;
  scope_ = {provenance.owner, provenance.accountEpoch};
  const time_t now = std::time(nullptr);
  StatsDaySlice day;
  if (!clockKnown || now <= 0 || !split(nullptr, uint64_t(now), day)) return true;
  today_ = day.day;
  remoteReady_ = adapter && adapter->cachedReadingStats(remote_) && remote_.scope == scope_ && scope_.owner != 0;
  const auto recovered = journal_.recover();
  if (recovered != storage::JournalResult::Ok) {
    LOG_ERR("BSW", "Stats history unavailable: %u", unsigned(recovered));
    options_.availability = StatsCardState::Unavailable;
    publish();
    return true;
  }
  options_.availability = StatsCardState::Ready;
  // Recovery and oldest row do not prove uninterrupted capture. Native rows
  // remain account-independent; no upload watermark is inferred from them.
  busy_ = stats_.begin(cursor_.freeze({}, 0, uint64_t(now), false), uint64_t(now), 0, {}, {split, nullptr, splitHour});
  if (!busy_) {
    options_.availability = StatsCardState::Unavailable;
    publish();
  }
  return !busy_;
}
void StatsWorker::publish() {
  const auto now = std::time(nullptr);
  options_.now = now > 0 ? uint64_t(now) : 0;
  stats_.combine(remoteReady_ ? &remote_ : nullptr, scope_, true, today_, snapshot_);
  if (published_.requested & statsWidgetBit(StatsWidget::Week)) {
    statsFreshness(options_, snapshot_.daysCoverage, now > 0 ? uint64_t(now) : 0);
    projectWeek(snapshot_, options_, published_.cards[unsigned(StatsWidget::Week)]);
  }
  if (published_.requested & statsWidgetBit(StatsWidget::Today)) {
    statsFreshness(options_,
                   snapshot_.nativeToday.known ? snapshot_.nativeTodayCoverage : snapshot_.accountTodayCoverage,
                   now > 0 ? uint64_t(now) : 0);
    projectToday(snapshot_, options_, published_.cards[unsigned(StatsWidget::Today)]);
  }
  if (published_.requested & statsWidgetBit(StatsWidget::Pages)) {
    statsFreshness(options_, snapshot_.recordsCoverage, now > 0 ? uint64_t(now) : 0);
    projectPages(snapshot_, options_, published_.cards[unsigned(StatsWidget::Pages)]);
  }
  if (published_.requested & statsWidgetBit(StatsWidget::Year)) {
    statsFreshness(options_, snapshot_.daysCoverage, now > 0 ? uint64_t(now) : 0);
    projectYear(snapshot_, options_, published_.cards[unsigned(StatsWidget::Year)], published_.year);
  }
  if (published_.requested & statsWidgetBit(StatsWidget::Monthly)) {
    statsFreshness(options_, snapshot_.daysCoverage, now > 0 ? uint64_t(now) : 0);
    projectMonthly(snapshot_, options_, published_.cards[unsigned(StatsWidget::Monthly)], published_.months);
  }
  if (published_.requested & statsWidgetBit(StatsWidget::When)) {
    statsFreshness(options_, snapshot_.hoursCoverage, options_.now);
    projectWhen(snapshot_, options_, published_.cards[unsigned(StatsWidget::When)], published_.hours);
  }
  if (published_.requested & statsWidgetBit(StatsWidget::Records)) {
    statsFreshness(options_, snapshot_.recordsCoverage, options_.now);
    projectRecords(snapshot_, options_, published_.cards[unsigned(StatsWidget::Records)], published_.records);
  }
  if (published_.requested & statsWidgetBit(StatsWidget::Streak)) {
    statsFreshness(options_, snapshot_.streakCoverage, now > 0 ? uint64_t(now) : 0);
    projectStreak(snapshot_, options_, published_.cards[unsigned(StatsWidget::Streak)]);
  }
}
bool StatsWorker::tick() {
  if (!busy_) return false;
  if (!stats_.tick()) {
    options_.availability = StatsCardState::Unavailable;
    busy_ = false;
  } else
    busy_ = stats_.busy();
  if (busy_) return false;
  publish();
  return true;
}
}  // namespace booksync
