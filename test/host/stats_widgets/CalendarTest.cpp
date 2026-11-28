// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include <chrono>

#include "booksync/services/widgets/Calendar.h"
#include "booksync/ui/widgets/Monthly.h"
#include "booksync/ui/widgets/Year.h"
using namespace booksync;
using namespace std::chrono;
namespace {
int64_t date(int y, unsigned m, unsigned d) { return sys_days{year{y} / month{m} / day{d}}.time_since_epoch().count(); }
class CalendarWidgets : public testing::TestWithParam<bool> {
 protected:
  StatsSnapshot stats{};
  StatsProjection options{};
  StatsCard card{};
  StatsYear heat{};
  StatsMonths months{};
  void SetUp() override {
    stats.firstDay = date(2026, 11, 27) - 364;
    stats.daysCoverage = {ReadingOrigin::ThisReader, {}, 1, 100, true, true};
    stats.days.fill({0, true});
    stats.days.back() = {3600, true};
  }
  void project() {
    if (GetParam())
      projectYear(stats, options, card, heat);
    else
      projectMonthly(stats, options, card, months);
  }
};
TEST_P(CalendarWidgets, Known) {
  project();
  EXPECT_EQ(card.state, StatsCardState::Ready);
  EXPECT_EQ(card.value.value, 3600);
}
TEST_P(CalendarWidgets, CompleteEmpty) {
  stats.days.fill({0, true});
  project();
  EXPECT_EQ(card.state, StatsCardState::Empty);
}
TEST_P(CalendarWidgets, MissingIsUnknown) {
  stats.days.fill({});
  project();
  EXPECT_EQ(card.state, StatsCardState::NotSaved);
  EXPECT_FALSE(card.value.known);
}
TEST_P(CalendarWidgets, Off) {
  options.enabled = false;
  project();
  EXPECT_EQ(card.state, StatsCardState::Off);
}
TEST_P(CalendarWidgets, Unsupported) {
  options.availability = StatsCardState::Unsupported;
  project();
  EXPECT_EQ(card.state, StatsCardState::Unsupported);
}
TEST_P(CalendarWidgets, StaleKeepsValues) {
  options.stale = true;
  options.ageDays = 2;
  project();
  EXPECT_TRUE(card.stale);
  EXPECT_EQ(card.ageDays, 2);
  EXPECT_EQ(card.value.value, 3600);
}
TEST_P(CalendarWidgets, Oversize) {
  options.availability = StatsCardState::Oversize;
  project();
  EXPECT_EQ(card.state, StatsCardState::Oversize);
}
TEST_P(CalendarWidgets, Error) {
  options.availability = StatsCardState::Unavailable;
  project();
  EXPECT_EQ(card.state, StatsCardState::Unavailable);
}
TEST_P(CalendarWidgets, RetiredAccount) {
  stats.daysCoverage = {ReadingOrigin::Account, {42, 3}, 1, 100, true, true};
  options.provenance.owner = 42;
  options.provenance.accountEpoch = 4;
  project();
  EXPECT_EQ(card.state, StatsCardState::NotSaved);
}
TEST_P(CalendarWidgets, Overflow) {
  stats.days.back() = {UINT64_MAX, true};
  stats.days[363] = {1, true};
  project();
  EXPECT_EQ(card.state, StatsCardState::Oversize);
}
INSTANTIATE_TEST_SUITE_P(YearAndMonthly, CalendarWidgets, testing::Bool());
TEST(StatsCalendar, LeapYearAndFutureAreExplicit) {
  StatsSnapshot stats;
  StatsProjection options;
  StatsCard card;
  StatsYear heat;
  stats.firstDay = date(2024, 3, 1) - 364;
  stats.days.fill({0, true});
  stats.days[363] = {7200, true};
  projectYear(stats, options, card, heat);
  EXPECT_EQ(heat.count, 366);
  EXPECT_EQ(heat.year, 2024);
  EXPECT_GT(heat.marks[59], 1);
  EXPECT_EQ(heat.marks[61], 6);
  EXPECT_EQ(card.best.value, 1);
}
TEST(StatsCalendar, UnknownMonthIsNotAZeroMonth) {
  StatsSnapshot stats;
  StatsProjection options;
  StatsCard card;
  StatsMonths months;
  stats.firstDay = date(2026, 11, 27) - 364;
  stats.days.back() = {3600, true};
  projectMonthly(stats, options, card, months);
  EXPECT_FALSE(months.totals[0].known);
  EXPECT_TRUE(months.totals[10].known);
  EXPECT_EQ(months.totals[10].value, 3600);
  EXPECT_FALSE(card.best.known);
  EXPECT_FALSE(card.complete);
}
TEST(StatsCalendar, LastMonthAcrossYearBoundary) {
  StatsSnapshot stats;
  StatsProjection options;
  StatsCard card;
  StatsMonths months;
  stats.firstDay = date(2026, 1, 2) - 364;
  stats.days.fill({0, true});
  stats.days[361] = {7200, true};
  projectMonthly(stats, options, card, months);
  EXPECT_EQ(card.best.value, 7200);
  EXPECT_TRUE(card.best.known);
  EXPECT_EQ(months.currentMonth, 0);
  EXPECT_EQ(card.value.value, 0);
}
}  // namespace

TEST(StatsCalendarDrawing, MainOptionalAndHostileBounds) {
  StatsCard statsValue;
  statsValue.state = StatsCardState::Ready;
  StatsYear heat;
  heat.count = 366;
  heat.firstWeekday = 6;
  heat.marks.fill(5);
  StatsMonths months;
  months.currentMonth = 11;
  months.totals.fill({UINT64_MAX, true});
  widgets::StatsMark mark;
  for (int height : {108, 224}) {
    for (unsigned day = 0; day < 366; ++day) {
      ASSERT_TRUE(widgets::Year::heatCell(statsValue, heat, {456, height, 18, false, true}, day, mark));
      EXPECT_GE(mark.x, height >= 180 ? 12 : 96);
      EXPECT_GE(mark.y, 0);
      EXPECT_LE(mark.x + mark.width, 456);
      EXPECT_LE(mark.y + mark.height, height - 8);
    }
  }
  for (unsigned month = 0; month < 12; ++month) {
    ASSERT_TRUE(widgets::Monthly::monthColumn(statsValue, months, {456, 108, 18, false, false}, month, mark));
    EXPECT_GE(mark.x, 110);
    EXPECT_GE(mark.y, 0);
    EXPECT_LE(mark.x + mark.width, 456);
    EXPECT_LE(mark.y + mark.height, 72);
  }
  EXPECT_FALSE(widgets::Year::heatCell(statsValue, heat, {224, 108, 18, false, false}, 0, mark));
  EXPECT_FALSE(widgets::Year::heatCell(statsValue, heat, {456, 108, 18, true, false}, 0, mark));
  heat.count = 367;
  EXPECT_FALSE(widgets::Year::heatCell(statsValue, heat, {456, 108, 18, false, true}, 0, mark));
  months.currentMonth = 12;
  EXPECT_FALSE(widgets::Monthly::monthColumn(statsValue, months, {456, 108, 18, false, false}, 0, mark));
}
