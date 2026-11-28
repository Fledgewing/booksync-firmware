// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "booksync/services/widgets/Streak.h"
#include "booksync/services/widgets/Week.h"
#include "booksync/ui/widgets/Streak.h"
#include "booksync/ui/widgets/Week.h"
using namespace booksync;
using Project = void (*)(const StatsSnapshot&, const StatsProjection&, StatsCard&);
class StatsWidgetStates : public testing::TestWithParam<Project> {
 protected:
  StatsSnapshot stats{};
  StatsProjection options{};
  StatsCard card{};
  void SetUp() override {
    stats.daysCoverage = stats.streakCoverage = {ReadingOrigin::ThisReader, {}, 1, 100, true, true};
    for (unsigned i = ReadingStats::Days - 7; i < ReadingStats::Days; ++i) stats.days[i] = {60, true};
    stats.streak = {5, true};
    stats.bestStreak = {12, true};
  }
  void project() { GetParam()(stats, options, card); }
};
TEST_P(StatsWidgetStates, Known) {
  project();
  EXPECT_EQ(card.state, StatsCardState::Ready);
  EXPECT_TRUE(card.value.known);
}
TEST_P(StatsWidgetStates, CompleteEmpty) {
  stats.days.fill({0, true});
  stats.streak = {0, true};
  stats.bestStreak = {0, true};
  project();
  EXPECT_EQ(card.state, StatsCardState::Empty);
  EXPECT_TRUE(card.value.known);
}
TEST_P(StatsWidgetStates, NotSaved) {
  stats.days.fill({});
  stats.streak = {};
  project();
  EXPECT_EQ(card.state, StatsCardState::NotSaved);
}
TEST_P(StatsWidgetStates, Off) {
  options.enabled = false;
  project();
  EXPECT_EQ(card.state, StatsCardState::Off);
}
TEST_P(StatsWidgetStates, Unsupported) {
  options.availability = StatsCardState::Unsupported;
  project();
  EXPECT_EQ(card.state, StatsCardState::Unsupported);
}
TEST_P(StatsWidgetStates, StaleKeepsActualValues) {
  options.stale = true;
  options.savedAt = 42;
  project();
  EXPECT_TRUE(card.stale);
  EXPECT_EQ(card.savedAt, 42);
  EXPECT_EQ(card.state, StatsCardState::Ready);
  EXPECT_TRUE(card.value.known);
}
TEST_P(StatsWidgetStates, Oversize) {
  options.availability = StatsCardState::Oversize;
  project();
  EXPECT_EQ(card.state, StatsCardState::Oversize);
}
TEST_P(StatsWidgetStates, FailedProvider) {
  options.availability = StatsCardState::Unavailable;
  project();
  EXPECT_EQ(card.state, StatsCardState::Unavailable);
}
TEST_P(StatsWidgetStates, RetiredAccount) {
  stats.daysCoverage = stats.streakCoverage = {ReadingOrigin::Account, {42, 3}, 1, 100, true, true};
  options.provenance.owner = 42;
  options.provenance.accountEpoch = 4;
  project();
  EXPECT_EQ(card.state, StatsCardState::NotSaved);
}
INSTANTIATE_TEST_SUITE_P(WeekAndStreak, StatsWidgetStates, testing::Values(projectWeek, projectStreak));
TEST(StatsWeek, PartialKnownDaysAreNotZero) {
  StatsSnapshot stats;
  StatsProjection options;
  StatsCard card;
  stats.days.back() = {600, true};
  projectWeek(stats, options, card);
  EXPECT_EQ(card.value.value, 600);
  EXPECT_EQ(card.state, StatsCardState::Ready);
  EXPECT_FALSE(card.days[0].known);
  EXPECT_FALSE(card.complete);
}
TEST(StatsWeek, OverflowIsRefused) {
  StatsSnapshot stats;
  StatsProjection options;
  StatsCard card;
  stats.days.back() = {UINT64_MAX, true};
  stats.days[363] = {1, true};
  projectWeek(stats, options, card);
  EXPECT_EQ(card.state, StatsCardState::Oversize);
}
TEST(StatsDrawing, MainSizeAndBounds) {
  StatsCard card;
  card.state = StatsCardState::Ready;
  card.days[3] = {UINT64_MAX, true};
  widgets::StatsDrawing drawing;
  for (auto draw : {widgets::Week::layoutStats, widgets::Streak::layoutStats}) {
    EXPECT_TRUE(draw(card, {224, 108, 18, false, true}, drawing));
    for (const auto& mark : drawing.marks) {
      EXPECT_GE(mark.x, 0);
      EXPECT_GE(mark.y, 0);
      EXPECT_LE(mark.x + mark.width, 224);
      EXPECT_LE(mark.y + mark.height, 108);
    }
    EXPECT_FALSE(drawing.marks[0].known);
    EXPECT_TRUE(drawing.marks[3].known);
    EXPECT_FALSE(draw(card, {224, 108, 18, true, false}, drawing));
    EXPECT_FALSE(draw(card, {60, 40, 18, false, false}, drawing));
    card.bytes++;
    EXPECT_FALSE(draw(card, {224, 108, 18, false, false}, drawing));
    card.bytes--;
  }
}

TEST(StatsWeek, MondayStartsANewCalendarWeekAndFutureDaysStayBlank) {
  StatsSnapshot stats;
  StatsProjection options;
  StatsCard card;
  stats.firstDay = 4 - ReadingStats::Days + 1;  // Monday 5 January 1970.
  stats.days[363] = {999, true};
  stats.days[364] = {60, true};
  projectWeek(stats, options, card);
  EXPECT_EQ(card.value.value, 60);
  EXPECT_EQ(card.currentDay, 0);
  EXPECT_EQ(card.firstWeekday, 1);
  EXPECT_EQ(card.minutes, 1);
  EXPECT_FALSE(card.days[1].known);
  widgets::StatsDrawing drawing;
  ASSERT_TRUE(widgets::Week::layoutStats(card, {224, 108, 18, false, true}, drawing));
  EXPECT_TRUE(drawing.marks[0].latestDay);
  EXPECT_FALSE(drawing.marks[0].future);
  EXPECT_TRUE(drawing.marks[1].future);
}

TEST(StatsStreak, KnownZeroCurrentStreakKeepsSavedBestVisible) {
  StatsSnapshot stats;
  StatsProjection options;
  StatsCard card;
  stats.streak = {0, true};
  stats.bestStreak = {12, true};
  projectStreak(stats, options, card);
  EXPECT_EQ(card.state, StatsCardState::Ready);
  EXPECT_TRUE(card.value.known);
  EXPECT_EQ(card.value.value, 0);
  EXPECT_EQ(card.best.value, 12);
}

TEST(StatsFreshness, MissingAndFutureTimestampsNeverBecomeInventedAges) {
  StatsProjection options;
  ReadingCoverage coverage{ReadingOrigin::Account, {}, 0, 0, true, true};
  statsFreshness(options, coverage, 2000000000);
  EXPECT_FALSE(options.stale);
  EXPECT_EQ(options.ageDays, 0);
  coverage.through = 2000000001;
  statsFreshness(options, coverage, 2000000000);
  EXPECT_FALSE(options.stale);
  EXPECT_EQ(options.ageDays, 0);
  coverage.through = 2000000000 - 2 * 86400;
  statsFreshness(options, coverage, 2000000000);
  EXPECT_TRUE(options.stale);
  EXPECT_EQ(options.ageDays, 2);
}

TEST(StatsDrawing, ApprovedStreakDotsSpanTheCardAndLeaveBestBelow) {
  StatsCard card;
  card.state = StatsCardState::Ready;
  card.complete = true;
  widgets::StatsDrawing drawing;
  ASSERT_TRUE(widgets::Streak::layoutStats(card, {224, 108, 18, false, false}, drawing));
  EXPECT_EQ(drawing.marks.front().x, 10);
  EXPECT_EQ(drawing.marks.back().x + drawing.marks.back().width, 214);
  EXPECT_EQ(drawing.marks.front().width, 10);
  EXPECT_EQ(drawing.marks.front().y, 74);
  card.stale = true;
  ASSERT_TRUE(widgets::Streak::layoutStats(card, {224, 108, 18, false, false}, drawing));
  EXPECT_EQ(drawing.marks.front().y, 60);
  card.stale = false;
  card.days[3] = {60, true};
  ASSERT_TRUE(widgets::Week::layoutStats(card, {224, 108, 18, false, false}, drawing));
  EXPECT_EQ(drawing.marks[3].height, 33);
  EXPECT_EQ(drawing.marks[3].y + drawing.marks[3].height, 85);
}
