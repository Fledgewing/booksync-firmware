// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Calendar.h"
#include "Forecast.h"
#include "Goal.h"
#include "Pages.h"
#include "Records.h"
#include "Streak.h"
#include "Today.h"
#include "Week.h"
#include "When.h"
#include "booksync/interface/Clock.h"
#include "booksync/services/ServerAdapter.h"
namespace booksync {
// Home-lifetime workspace: fixed buffers exceed task stacks and are released
// before reader entry. One bounded cursor tick; no work via public read methods.
class StatsWorker {
 public:
  StatsWorker() : cursor_(journal_) {}
  bool begin(uint16_t requested, Provenance provenance, const ClockStatus&, ServerAdapter*);
  bool tick(const ClockStatus&);
  void retire() {
    busy_ = false;
    published_.requested = 0;
  }
  const StatsWidgetBatch& published() const { return published_; }

 private:
  storage::PageJournal journal_{"/.booksync/pages"};
  PageStatsSource cursor_;
  ReadingStats stats_;
  StatsSnapshot snapshot_;
  RemoteReadingStats remote_;
  ReadingForecast forecast_;
  StatsWidgetBatch published_{};
  StatsProjection options_{};
  ReadingScope scope_{};
  int64_t today_ = 0;
  ServerAdapter* adapter_ = nullptr;
  ClockStatus clock_{};
  std::array<char, 64> timezone_{};
  uint64_t refreshedAt_ = 0;
  bool busy_ = false, remoteReady_ = false, forecastReady_ = false, localFailed_ = false;
  void loadSaved();
  void freshness(const ReadingCoverage&);
  void clearCards();
  static bool split(void*, uint64_t, StatsDaySlice&);
  static bool splitHour(void*, uint64_t, StatsHourSlice&);
  void publish();
};
static_assert(sizeof(StatsWorker) <= 30 * 1024);
}  // namespace booksync
