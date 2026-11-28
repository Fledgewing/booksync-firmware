// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include <cstdlib>
#include <ctime>
#include <string>

#include "booksync/services/widgets/StatsWorker.h"
using namespace booksync;
namespace {
constexpr uint64_t Noon = 1795867200;
ClockStatus at(uint64_t now) {
  ClockStatus out;
  out.evidence = ClockEvidence::Retained;
  out.utcSeconds = int64_t(now);
  const time_t stamp = time_t(now);
  std::tm date{};
  localtime_r(&stamp, &date);
  out.local = {uint16_t(date.tm_year + 1900), uint8_t(date.tm_mon + 1), uint8_t(date.tm_mday), uint8_t(date.tm_wday),
               uint8_t(date.tm_hour),         uint8_t(date.tm_min),     uint8_t(date.tm_sec)};
  return out;
}
class Saved final : public ServerAdapter {
 public:
  RemoteReadingStats stats;
  ReadingForecast forecast;
  mutable unsigned statsReads = 0, forecastReads = 0;
  bool hasForecast = false;
  Capabilities capabilities() const override { return {}; }
  Snapshot cachedSnapshot() const override { return {}; }
  CommandStatus deliver(const SyncWriterToken&, Intent) override {
    ADD_FAILURE() << "read-only source dispatched";
    return CommandStatus::Unavailable;
  }
  bool cachedReadingStats(RemoteReadingStats& out) const override {
    ++statsReads;
    out = stats;
    return true;
  }
  bool cachedReadingForecast(ReadingForecast& out) const override {
    ++forecastReads;
    if (!hasForecast) return false;
    out = forecast;
    return true;
  }
};
class StatsWorkerTest : public testing::Test {
 protected:
  StatsWorker worker;
  Saved saved;
  Provenance provenance{};
  std::string oldZone;
  bool hadZone = false;
  void SetUp() override {
    const auto* zone = std::getenv("TZ");
    hadZone = zone;
    oldZone = zone ? zone : "";
    setenv("TZ", "UTC0", 1);
    tzset();
    card = {};
    provenance.owner = 7;
    provenance.accountEpoch = 2;
    saved.stats.scope = {7, 2};
    saved.stats.goalAvailable = true;
    saved.stats.finished = {18, true};
    saved.stats.target = {24, true};
    saved.stats.year = {2026, true};
    saved.stats.snapshotAt = Noon;
  }
  void TearDown() override {
    if (hadZone)
      setenv("TZ", oldZone.c_str(), 1);
    else
      unsetenv("TZ");
    tzset();
  }
  void add() {
    storage::PageJournal journal("/.booksync/pages");
    ASSERT_EQ(journal.recover(), storage::JournalResult::Ok);
    storage::PageEvent e{1, Noon - 60, 1, 30, 0, 1, 10, 0, 1, 10, 0};
    ASSERT_EQ(journal.append(e), storage::JournalResult::Ok);
  }
  void finish(ClockStatus clock) {
    for (unsigned i = 0; i < 8; ++i)
      if (worker.tick(clock)) return;
    FAIL() << "bounded fixture never published";
  }
  const StatsCard& value(StatsWidget kind) { return worker.published().cards[unsigned(kind)]; }
};
TEST_F(StatsWorkerTest, MissingClockDoesNoIoAndRevocationClearsSavedValues) {
  const auto before = card.events.size();
  worker.begin(0x3ff, provenance, {}, &saved);
  EXPECT_EQ(card.events.size(), before);
  EXPECT_EQ(saved.statsReads, 0);
  add();
  ASSERT_FALSE(worker.begin(0x3ff, provenance, at(Noon), &saved));
  finish(at(Noon));
  ASSERT_EQ(value(StatsWidget::Pages).value.value, 1);
  const auto reads = card.events.size();
  EXPECT_TRUE(worker.tick({}));
  EXPECT_EQ(card.events.size(), reads);
  for (const auto& value : worker.published().cards) EXPECT_EQ(value.state, StatsCardState::NotSaved);
  EXPECT_FALSE(worker.tick({}));
}
TEST_F(StatsWorkerTest, MinuteRefreshReadsOnlySavedValuesAndNoJournal) {
  add();
  ASSERT_FALSE(worker.begin(0x3ff, provenance, at(Noon), &saved));
  finish(at(Noon));
  const auto events = card.events.size();
  const auto reads = saved.statsReads;
  EXPECT_FALSE(worker.tick(at(Noon + 59)));
  EXPECT_EQ(saved.statsReads, reads);
  saved.stats.finished.value = 20;
  EXPECT_TRUE(worker.tick(at(Noon + 60)));
  EXPECT_EQ(saved.statsReads, reads + 1);
  EXPECT_EQ(card.events.size(), events);
  EXPECT_EQ(value(StatsWidget::Goal).value.value, 20);
  EXPECT_EQ(value(StatsWidget::Pages).value.value, 1);
}
TEST_F(StatsWorkerTest, DateRolloverRebuildsWithoutInventingTodayZero) {
  add();
  worker.begin(0x3ff, provenance, at(Noon), &saved);
  finish(at(Noon));
  ASSERT_TRUE(value(StatsWidget::Pages).value.known);
  EXPECT_TRUE(worker.tick(at(Noon + 86400)));
  finish(at(Noon + 86400));
  EXPECT_FALSE(value(StatsWidget::Pages).value.known);
  EXPECT_EQ(value(StatsWidget::Pages).best.value, 1);
}
TEST_F(StatsWorkerTest, NativeHistoryFailureDoesNotHideHealthyAccountGoal) {
  card.present = false;
  EXPECT_TRUE(worker.begin(0x3ff, provenance, at(Noon), &saved));
  EXPECT_EQ(value(StatsWidget::Goal).state, StatsCardState::Ready);
  EXPECT_EQ(value(StatsWidget::Goal).value.value, 18);
  EXPECT_EQ(value(StatsWidget::Pages).state, StatsCardState::Unavailable);
  EXPECT_EQ(value(StatsWidget::Forecast).state, StatsCardState::NotSaved);
}
TEST_F(StatsWorkerTest, TimezoneChangeRebuildsHistogram) {
  add();
  worker.begin(0x3ff, provenance, at(Noon), &saved);
  finish(at(Noon));
  const auto hour = worker.published().hours.peakHour;
  setenv("TZ", "UTC-2", 1);
  tzset();
  EXPECT_TRUE(worker.tick(at(Noon)));
  finish(at(Noon));
  EXPECT_EQ(worker.published().hours.peakHour, (hour + 2) % 24);
}
}  // namespace

TEST_F(StatsWorkerTest, SavedQualifiedForecastSurvivesNativeJournalFailure) {
  card.present = false;
  saved.hasForecast = true;
  auto& source = saved.forecast;
  source.current.identityKnown = true;
  source.observed = source.current.key;
  source.matchingEdition = true;
  source.measuredVisits = 5;
  source.card.progress = {40, true};
  source.card.timeLeft = {3600, true};
  source.card.progressCoverage = source.card.timeLeftCoverage = {ReadingOrigin::ThisReader, {}, 1, Noon, true, true};
  EXPECT_TRUE(worker.begin(0x3ff, provenance, at(Noon), &saved));
  EXPECT_EQ(value(StatsWidget::Forecast).state, StatsCardState::Ready);
  EXPECT_EQ(value(StatsWidget::Forecast).hours, 1);
  EXPECT_EQ(value(StatsWidget::Pages).state, StatsCardState::Unavailable);
}

TEST_F(StatsWorkerTest, RetiredVisibleMaskStopsCursorAndMinuteCacheWork) {
  add();
  ASSERT_FALSE(worker.begin(0x3ff, provenance, at(Noon), &saved));
  worker.retire();
  const auto events = card.events.size();
  const auto reads = saved.statsReads;
  EXPECT_FALSE(worker.tick(at(Noon + 120)));
  EXPECT_EQ(worker.published().requested, 0);
  EXPECT_EQ(card.events.size(), events);
  EXPECT_EQ(saved.statsReads, reads);
}
