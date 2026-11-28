// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "booksync/services/widgets/Forecast.h"
#include "booksync/services/widgets/Goal.h"
#include "booksync/ui/widgets/Forecast.h"
#include "booksync/ui/widgets/Goal.h"
using namespace booksync;
namespace {
class GoalForecastStates : public testing::TestWithParam<bool> {
 protected:
  StatsSnapshot stats;
  ReadingForecast forecast;
  StatsProjection options;
  StatsCard out;
  StatsGoal goal;
  StatsForecast detail;
  void SetUp() override {
    options.provenance.owner = 7;
    options.provenance.accountEpoch = 2;
    options.today = 20785;
    stats.firstDay = options.today - ReadingStats::Days + 1;
    stats.finished = {18, true};
    stats.target = {24, true};
    stats.year = {2026, true};
    stats.goalCoverage = stats.finishedCoverage = {ReadingOrigin::Account, {7, 2}, 1, 100, true, true};
    forecast.current.key.provenance = options.provenance;
    forecast.current.identityKnown = true;
    forecast.observed = forecast.current.key;
    forecast.card.progress = {36, true};
    forecast.card.timeLeft = {7920, true};
    forecast.card.finishDay = {20787, true};
    forecast.card.timeLeftCoverage = {ReadingOrigin::ThisReader, {}, 1, 100, true, true};
    forecast.card.progressCoverage = forecast.card.finishDayCoverage = forecast.card.timeLeftCoverage;
    forecast.measuredVisits = 5;
    forecast.matchingEdition = true;
  }
  void project() {
    if (GetParam())
      projectGoal(stats, options, out, goal);
    else
      projectForecast(forecast, options, out, detail);
  }
};
TEST_P(GoalForecastStates, Known) {
  project();
  EXPECT_EQ(out.state, StatsCardState::Ready);
  EXPECT_TRUE(out.value.known);
}
TEST_P(GoalForecastStates, Empty) {
  stats.target.value = 0;
  forecast.card.progress.value = 100;
  forecast.measuredVisits = 0;
  project();
  EXPECT_EQ(out.state, StatsCardState::Empty);
}
TEST_P(GoalForecastStates, NotSaved) {
  stats.target.known = false;
  forecast.measuredVisits = 4;
  project();
  EXPECT_EQ(out.state, StatsCardState::NotSaved);
}
TEST_P(GoalForecastStates, Off) {
  options.enabled = false;
  project();
  EXPECT_EQ(out.state, StatsCardState::Off);
}
TEST_P(GoalForecastStates, Unsupported) {
  options.availability = StatsCardState::Unsupported;
  project();
  EXPECT_EQ(out.state, StatsCardState::Unsupported);
}
TEST_P(GoalForecastStates, Stale) {
  options.stale = true;
  options.ageDays = 2;
  project();
  EXPECT_EQ(out.state, StatsCardState::Ready);
  EXPECT_TRUE(out.stale);
  EXPECT_EQ(out.ageDays, 2);
}
TEST_P(GoalForecastStates, Oversize) {
  options.availability = StatsCardState::Oversize;
  project();
  EXPECT_EQ(out.state, StatsCardState::Oversize);
}
TEST_P(GoalForecastStates, Error) {
  options.availability = StatsCardState::Unavailable;
  project();
  EXPECT_EQ(out.state, StatsCardState::Unavailable);
}
TEST_P(GoalForecastStates, WrongEpoch) {
  options.provenance.accountEpoch = 3;
  project();
  EXPECT_EQ(out.state, StatsCardState::NotSaved);
}
INSTANTIATE_TEST_SUITE_P(GoalAndForecast, GoalForecastStates, testing::Bool());
TEST(StatsGoal, KnownZeroAndPaceRemainAccountYearScoped) {
  StatsSnapshot stats;
  StatsProjection options;
  StatsCard out;
  StatsGoal goal;
  options.provenance.owner = 7;
  options.provenance.accountEpoch = 2;
  stats.firstDay = 20785 - ReadingStats::Days + 1;
  stats.finished = {0, true};
  stats.target = {24, true};
  stats.year = {2026, true};
  stats.goalCoverage = stats.finishedCoverage = {ReadingOrigin::Account, {7, 2}, 1, 100, true, true};
  projectGoal(stats, options, out, goal);
  EXPECT_EQ(out.state, StatsCardState::Ready);
  EXPECT_TRUE(out.value.known);
  EXPECT_EQ(out.value.value, 0);
  EXPECT_GT(goal.paceTenths, 0);
  EXPECT_EQ(goal.percent, 0);
  stats.year.value = 2025;
  projectGoal(stats, options, out, goal);
  EXPECT_EQ(out.state, StatsCardState::NotSaved);
  stats.year.value = 2026;
  stats.finishedCoverage.scope.epoch = 1;
  projectGoal(stats, options, out, goal);
  EXPECT_EQ(out.state, StatsCardState::NotSaved);
}
TEST(StatsForecast, ExactBookEditionProgressAndFiveVisitsAreRequired) {
  ReadingForecast source;
  StatsProjection options;
  StatsCard out;
  StatsForecast detail;
  source.current.identityKnown = true;
  source.current.key.provenance = options.provenance;
  source.observed = source.current.key;
  source.card.timeLeftCoverage = {ReadingOrigin::ThisReader, {}, 1, 100, true, true};
  source.card.progressCoverage = source.card.finishDayCoverage = source.card.timeLeftCoverage;
  source.card.progress = {40, true};
  source.card.timeLeft = {3600, true};
  source.measuredVisits = 5;
  source.matchingEdition = true;
  projectForecast(source, options, out, detail);
  EXPECT_EQ(out.state, StatsCardState::Ready);
  EXPECT_FALSE(detail.dateKnown);
  source.observed.ordinal++;
  projectForecast(source, options, out, detail);
  EXPECT_EQ(out.state, StatsCardState::NotSaved);
  source.observed = source.current.key;
  source.matchingEdition = false;
  projectForecast(source, options, out, detail);
  EXPECT_EQ(out.state, StatsCardState::NotSaved);
  source.matchingEdition = true;
  source.card.progress.known = false;
  projectForecast(source, options, out, detail);
  EXPECT_EQ(out.state, StatsCardState::NotSaved);
  source.card.progress = {101, true};
  projectForecast(source, options, out, detail);
  EXPECT_EQ(out.state, StatsCardState::NotSaved);
  source.card.progress = {40, true};
  source.measuredVisits = 4;
  projectForecast(source, options, out, detail);
  EXPECT_EQ(out.state, StatsCardState::NotSaved);
}
TEST(StatsGoal, HugeInputsDoNotWrapPace) {
  StatsSnapshot stats;
  StatsProjection options;
  StatsCard out;
  StatsGoal goal;
  options.provenance.owner = 7;
  options.provenance.accountEpoch = 2;
  stats.firstDay = 20785 - ReadingStats::Days + 1;
  stats.finished = {0, true};
  stats.target = {UINT64_MAX, true};
  stats.year = {2026, true};
  stats.goalCoverage = stats.finishedCoverage = {ReadingOrigin::Account, {7, 2}, 1, 100, true, true};
  projectGoal(stats, options, out, goal);
  EXPECT_EQ(out.state, StatsCardState::Oversize);
}
}  // namespace

TEST(GoalForecastGeometry, ProfilesSizesAndStates) {
  StatsCard statsValue;
  statsValue.state = StatsCardState::Ready;
  StatsGoal goal;
  goal.percent = 75;
  widgets::StatsMark mark;
  widgets::StatsSummary summary;
  for (bool touch : {false, true})
    for (int height : {80, 96, 108})
      for (bool stale : {false, true}) {
        statsValue.stale = stale;
        const WidgetBox box{224, height, 18, false, touch};
        ASSERT_TRUE(widgets::Goal::progressBar(statsValue, goal, box, mark));
        EXPECT_EQ(mark.width, 153);
        EXPECT_GT(mark.height, 0);
        EXPECT_LT(mark.y + mark.height, height);
        ASSERT_TRUE(widgets::Forecast::summaryPositions(statsValue, box, summary));
        EXPECT_GT(summary.captionY, summary.numberY);
        EXPECT_GT(summary.sourceY, summary.captionY);
        EXPECT_LT(summary.footerY, height);
      }
  EXPECT_FALSE(widgets::Goal::progressBar(statsValue, goal, {224, 108, 18, true, false}, mark));
  EXPECT_FALSE(widgets::Forecast::summaryPositions(statsValue, {224, 108, 18, true, false}, summary));
  for (auto state : {StatsCardState::NotSaved, StatsCardState::Empty, StatsCardState::Off, StatsCardState::Unsupported,
                     StatsCardState::Unavailable, StatsCardState::Oversize}) {
    statsValue.state = state;
    EXPECT_FALSE(widgets::Goal::progressBar(statsValue, goal, {224, 108, 18, false, false}, mark));
    EXPECT_FALSE(widgets::Forecast::summaryPositions(statsValue, {224, 108, 18, false, false}, summary));
  }
  statsValue.state = StatsCardState::Ready;
  statsValue.bytes++;
  EXPECT_FALSE(widgets::Goal::progressBar(statsValue, goal, {224, 108, 18, false, false}, mark));
  EXPECT_FALSE(widgets::Forecast::summaryPositions(statsValue, {224, 108, 18, false, false}, summary));
}
TEST(StatsForecast, RoundWholeMinutesAndRejectUnprovenDate) {
  ReadingForecast source;
  StatsProjection options;
  StatsCard out;
  StatsForecast detail;
  source.current.identityKnown = true;
  source.current.key.provenance = options.provenance;
  source.observed = source.current.key;
  source.matchingEdition = true;
  source.measuredVisits = 5;
  source.card.progress = {40, true};
  source.card.timeLeft = {3599, true};
  source.card.timeLeftCoverage = {ReadingOrigin::ThisReader, {}, 1, 100, true, true};
  source.card.progressCoverage = source.card.finishDayCoverage = source.card.timeLeftCoverage;
  options.today = 20785;
  source.card.finishDay = {20786, true};
  projectForecast(source, options, out, detail);
  EXPECT_EQ(out.state, StatsCardState::Ready);
  EXPECT_EQ(out.hours, 1);
  EXPECT_EQ(out.minutes, 0);
  EXPECT_TRUE(detail.dateKnown);
  EXPECT_EQ(detail.weekday, 0);
  source.card.finishDay.value = 20784;
  projectForecast(source, options, out, detail);
  EXPECT_FALSE(detail.dateKnown);
  EXPECT_EQ(out.state, StatsCardState::Ready);
  source.card.timeLeftCoverage.saved = false;
  projectForecast(source, options, out, detail);
  EXPECT_EQ(out.state, StatsCardState::NotSaved);
}

TEST(StatsForecast, BoundNativeLocatorAndPaceCannotCrossAccounts) {
  ReadingForecast source;
  StatsProjection options;
  StatsCard out;
  StatsForecast detail;
  options.provenance.owner = 7;
  options.provenance.accountEpoch = 2;
  options.today = 20785;
  source.current.identityKnown = true;
  source.observed = source.current.key;
  source.matchingEdition = true;
  source.measuredVisits = 5;
  source.card.progress = {40, true};
  source.card.timeLeft = {3600, true};
  source.card.finishDay = {20786, true};
  source.card.progressCoverage = source.card.timeLeftCoverage =
      source.card.finishDayCoverage = {ReadingOrigin::ThisReader, {}, 1, 100, true, true};
  source.card.timeLeftCoverage.scope = {7, 1};
  projectForecast(source, options, out, detail);
  EXPECT_EQ(out.state, StatsCardState::NotSaved);
  source.card.timeLeftCoverage.scope = {};
  source.card.progressCoverage.saved = false;
  projectForecast(source, options, out, detail);
  EXPECT_EQ(out.state, StatsCardState::NotSaved);
  source.card.progressCoverage.saved = true;
  source.card.finishDayCoverage.scope = {7, 1};
  projectForecast(source, options, out, detail);
  EXPECT_EQ(out.state, StatsCardState::Ready);
  EXPECT_FALSE(detail.dateKnown);
  source.card.finishDayCoverage.scope = {};
  projectForecast(source, options, out, detail);
  EXPECT_TRUE(detail.dateKnown);
}
