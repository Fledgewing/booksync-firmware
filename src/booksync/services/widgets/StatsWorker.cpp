// SPDX-License-Identifier: GPL-3.0-or-later
#include "StatsWorker.h"

#include <Logging.h>

#include <cstdlib>
#include <cstring>
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
bool StatsWorker::begin(uint16_t requested, Provenance provenance, const ClockStatus& clock, ServerAdapter* adapter) {
  busy_ = false;
  published_.requested = requested;
  clearCards();
  clock_ = clock;
  adapter_ = adapter;
  localFailed_ = false;
  const auto* zone = std::getenv("TZ");
  std::snprintf(timezone_.data(), timezone_.size(), "%s", zone ? zone : "");
  options_.provenance = provenance;
  scope_ = {provenance.owner, provenance.accountEpoch};
  const auto now = clock.utcSeconds;
  StatsDaySlice day;
  if (!clock.hasDateTime() || !split(nullptr, uint64_t(now), day)) return true;
  today_ = day.day;
  loadSaved();
  const auto recovered = Storage.ready() ? journal_.recover() : storage::JournalResult::IoError;
  if (recovered != storage::JournalResult::Ok) {
    LOG_ERR("BSW", "Stats history unavailable: %u", unsigned(recovered));
    localFailed_ = true;
    stats_.discard();
    publish();
    return true;
  }
  options_.availability = StatsCardState::Ready;
  // Recovery and oldest row do not prove uninterrupted capture. Native rows
  // remain account-independent; no upload watermark is inferred from them.
  busy_ = stats_.begin(cursor_.freeze({}, 0, uint64_t(now), false), uint64_t(now), 0, {}, {split, nullptr, splitHour});
  if (!busy_) {
    localFailed_ = true;
    publish();
  }
  return !busy_;
}
void StatsWorker::publish() {
  const auto now = clock_.utcSeconds;
  options_.now = now > 0 ? uint64_t(now) : 0;
  options_.today = today_;
  stats_.combine(remoteReady_ ? &remote_ : nullptr, scope_, true, today_, snapshot_);
  if (published_.requested & statsWidgetBit(StatsWidget::Week)) {
    freshness(snapshot_.daysCoverage);
    projectWeek(snapshot_, options_, published_.cards[unsigned(StatsWidget::Week)]);
  }
  if (published_.requested & statsWidgetBit(StatsWidget::Today)) {
    freshness(snapshot_.nativeToday.known ? snapshot_.nativeTodayCoverage : snapshot_.accountTodayCoverage);
    projectToday(snapshot_, options_, published_.cards[unsigned(StatsWidget::Today)]);
  }
  if (published_.requested & statsWidgetBit(StatsWidget::Pages)) {
    freshness(snapshot_.recordsCoverage);
    projectPages(snapshot_, options_, published_.cards[unsigned(StatsWidget::Pages)]);
  }
  if (published_.requested & statsWidgetBit(StatsWidget::Year)) {
    freshness(snapshot_.daysCoverage);
    projectYear(snapshot_, options_, published_.cards[unsigned(StatsWidget::Year)], published_.year);
  }
  if (published_.requested & statsWidgetBit(StatsWidget::Monthly)) {
    freshness(snapshot_.daysCoverage);
    projectMonthly(snapshot_, options_, published_.cards[unsigned(StatsWidget::Monthly)], published_.months);
  }
  if (published_.requested & statsWidgetBit(StatsWidget::When)) {
    freshness(snapshot_.hoursCoverage);
    projectWhen(snapshot_, options_, published_.cards[unsigned(StatsWidget::When)], published_.hours);
  }
  if (published_.requested & statsWidgetBit(StatsWidget::Records)) {
    freshness(snapshot_.recordsCoverage.saved ? snapshot_.recordsCoverage : snapshot_.finishedCoverage);
    projectRecords(snapshot_, options_, published_.cards[unsigned(StatsWidget::Records)], published_.records);
  }
  if (published_.requested & statsWidgetBit(StatsWidget::Goal)) {
    freshness(snapshot_.goalCoverage);
    projectGoal(snapshot_, options_, published_.cards[unsigned(StatsWidget::Goal)], published_.goal);
  }
  if (published_.requested & statsWidgetBit(StatsWidget::Forecast)) {
    if (forecastReady_) {
      options_.availability = StatsCardState::Ready;
      statsFreshness(options_, forecast_.card.timeLeftCoverage, options_.now);
      projectForecast(forecast_, options_, published_.cards[unsigned(StatsWidget::Forecast)], published_.forecast);
    } else {
      auto& value = published_.cards[unsigned(StatsWidget::Forecast)];
      value.clear();
      value.provenance = options_.provenance;
      published_.forecast = {};
    }
  }
  if (published_.requested & statsWidgetBit(StatsWidget::Streak)) {
    freshness(snapshot_.streakCoverage);
    projectStreak(snapshot_, options_, published_.cards[unsigned(StatsWidget::Streak)]);
  }
}
void StatsWorker::clearCards() {
  for (auto& value : published_.cards) value.clear();
}
void StatsWorker::freshness(const ReadingCoverage& coverage) {
  options_.availability = localFailed_ && (!coverage.saved || coverage.origin == ReadingOrigin::ThisReader)
                              ? StatsCardState::Unavailable
                              : StatsCardState::Ready;
  statsFreshness(options_, coverage, options_.now);
}
void StatsWorker::loadSaved() {
  refreshedAt_ = clock_.utcSeconds > 0 ? uint64_t(clock_.utcSeconds) : 0;
  remoteReady_ = adapter_ && adapter_->cachedReadingStats(remote_) && remote_.scope == scope_ && scope_.owner != 0;
  forecastReady_ = adapter_ && (published_.requested & statsWidgetBit(StatsWidget::Forecast)) &&
                   adapter_->cachedReadingForecast(forecast_);
}
bool StatsWorker::tick(const ClockStatus& clock) {
  if (!published_.requested) return false;
  if (!clock.hasDateTime()) {
    if (!clock_.hasDateTime()) return false;
    clock_ = {};
    busy_ = false;
    clearCards();
    return true;
  }
  const auto* zone = std::getenv("TZ");
  const bool changed = clock.local.year != clock_.local.year || clock.local.month != clock_.local.month ||
                       clock.local.day != clock_.local.day || clock.utcOffsetSeconds != clock_.utcOffsetSeconds ||
                       clock.utcSeconds < clock_.utcSeconds ||
                       std::strncmp(timezone_.data(), zone ? zone : "", timezone_.size()) != 0;
  if (!clock_.hasDateTime() || changed) {
    begin(published_.requested, options_.provenance, clock, adapter_);
    return true;
  }
  clock_ = clock;
  if (!busy_) {
    if (uint64_t(clock.utcSeconds) - refreshedAt_ < 60) return false;
    loadSaved();
    publish();
    return true;
  }
  if (!stats_.tick()) {
    localFailed_ = true;
    stats_.discard();
    busy_ = false;
  } else
    busy_ = stats_.busy();
  if (busy_) return false;
  publish();
  return true;
}
}  // namespace booksync
