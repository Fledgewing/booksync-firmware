// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "booksync/services/widgets/Records.h"
#include "booksync/services/widgets/When.h"
#include "booksync/ui/widgets/Records.h"
#include "booksync/ui/widgets/When.h"
using namespace booksync;
namespace {
class WhenRecordsStates : public testing::TestWithParam<bool> {
 protected:
  StatsSnapshot stats;
  StatsProjection options;
  StatsCard out;
  StatsHours hours;
  StatsRecords records;
  void SetUp() override {
    options.provenance.owner = 7;
    options.provenance.accountEpoch = 2;
    stats.hoursAvailable = true;
    stats.hoursKnown = 0xffffff;
    stats.hours[9] = 2160;
    stats.hours[20] = 3840;
    stats.recordedPages = {48210, true};
    stats.longestSession = {11520, true};
    stats.bestDayPages = {412, true};
    stats.finished = {18, true};
    stats.bestStreak = {12, true};
    stats.hoursCoverage = stats.recordsCoverage =
        stats.streakCoverage = {ReadingOrigin::ThisReader, {}, 1, 100, true, true};
    stats.finishedCoverage = {ReadingOrigin::Account, {7, 2}, 1, 100, true, true};
  }
  void project() {
    if (GetParam())
      projectWhen(stats, options, out, hours);
    else
      projectRecords(stats, options, out, records);
  }
};
TEST_P(WhenRecordsStates, Known) {
  project();
  EXPECT_EQ(out.state, StatsCardState::Ready);
  EXPECT_TRUE(out.value.known);
}
TEST_P(WhenRecordsStates, Empty) {
  stats.hours.fill(0);
  stats.recordedPages.value = stats.longestSession.value = stats.bestDayPages.value = stats.finished.value =
      stats.bestStreak.value = 0;
  project();
  EXPECT_EQ(out.state, StatsCardState::Empty);
}
TEST_P(WhenRecordsStates, NotSaved) {
  stats.hoursKnown = 0;
  stats.recordedPages = stats.longestSession = stats.bestDayPages = stats.finished = stats.bestStreak = {};
  project();
  EXPECT_EQ(out.state, StatsCardState::NotSaved);
}
TEST_P(WhenRecordsStates, Off) {
  options.enabled = false;
  project();
  EXPECT_EQ(out.state, StatsCardState::Off);
}
TEST_P(WhenRecordsStates, Unsupported) {
  options.availability = StatsCardState::Unsupported;
  project();
  EXPECT_EQ(out.state, StatsCardState::Unsupported);
}
TEST_P(WhenRecordsStates, Stale) {
  options.stale = true;
  options.ageDays = 2;
  project();
  EXPECT_EQ(out.state, StatsCardState::Ready);
  EXPECT_TRUE(out.stale);
  EXPECT_EQ(out.ageDays, 2);
}
TEST_P(WhenRecordsStates, Oversize) {
  options.availability = StatsCardState::Oversize;
  project();
  EXPECT_EQ(out.state, StatsCardState::Oversize);
}
TEST_P(WhenRecordsStates, Error) {
  options.availability = StatsCardState::Unavailable;
  project();
  EXPECT_EQ(out.state, StatsCardState::Unavailable);
}
TEST_P(WhenRecordsStates, RetiredAccount) {
  stats.hoursCoverage = stats.recordsCoverage = {ReadingOrigin::Account, {7, 1}, 1, 100, true, true};
  project();
  EXPECT_EQ(out.state, StatsCardState::NotSaved);
}
INSTANTIATE_TEST_SUITE_P(HoursAndRecords, WhenRecordsStates, testing::Bool());
TEST(StatsWhen, EarliestPeakTieAndAfter18UseOnlyKnownTime) {
  StatsSnapshot stats;
  StatsProjection options;
  StatsCard out;
  StatsHours hours;
  stats.hoursAvailable = true;
  stats.hoursKnown = (1u << 6) | (1u << 20);
  stats.hours[6] = stats.hours[20] = 60;
  stats.hours[7] = 999;
  projectWhen(stats, options, out, hours);
  EXPECT_EQ(hours.peakHour, 6);
  EXPECT_EQ(hours.after18, 50);
  EXPECT_FALSE(hours.totals[7].known);
  EXPECT_EQ(out.value.value, 120);
}
TEST(StatsWhen, OverflowCannotWrapPercentage) {
  StatsSnapshot stats;
  StatsProjection options;
  StatsCard out;
  StatsHours hours;
  stats.hoursAvailable = true;
  stats.hoursKnown = 1;
  stats.hours[0] = UINT64_MAX;
  projectWhen(stats, options, out, hours);
  EXPECT_EQ(out.state, StatsCardState::Oversize);
}
TEST(StatsRecords, MixedFieldsCarryScopeAndOldestAccountAge) {
  StatsSnapshot stats;
  StatsProjection options;
  StatsCard out;
  StatsRecords records;
  options.provenance.owner = 7;
  options.provenance.accountEpoch = 2;
  options.now = 200000;
  stats.recordedPages = {0, true};
  stats.recordsCoverage = {ReadingOrigin::ThisReader, {}, 1, 200000, true, true};
  stats.finished = {18, true};
  stats.finishedCoverage = {ReadingOrigin::Account, {7, 2}, 1, 20000, true, true};
  projectRecords(stats, options, out, records);
  EXPECT_EQ(out.state, StatsCardState::Ready);
  EXPECT_TRUE(records.values[0].known);
  EXPECT_EQ(records.values[0].value, 0);
  EXPECT_FALSE(records.values[1].known);
  EXPECT_EQ(records.values[3].value, 18);
  EXPECT_EQ(records.sources[3], StatsCardSource::Account);
  EXPECT_TRUE(out.accountLinked);
  EXPECT_TRUE(out.stale);
  EXPECT_EQ(out.ageDays, 2);
  options.provenance.accountEpoch = 3;
  projectRecords(stats, options, out, records);
  EXPECT_FALSE(records.values[3].known);
  EXPECT_FALSE(out.accountLinked);
  EXPECT_TRUE(records.values[0].known);
}
}  // namespace

TEST(WhenRecordsGeometry, ProfilesStatesAndAllColumns) {
  StatsCard card;
  card.state = StatsCardState::Ready;
  StatsHours hours;
  hours.totals[20] = {60, true};
  hours.peakHour = 20;
  hours.peakKnown = true;
  widgets::StatsMark mark;
  for (bool touch : {false, true})
    for (int height : {80, 96, 108})
      for (bool stale : {false, true}) {
        card.stale = stale;
        for (unsigned h = 0; h < 24; ++h) {
          ASSERT_TRUE(widgets::When::hourColumn(card, hours, {224, height, 18, false, touch}, h, mark));
          EXPECT_GE(mark.x, 10);
          EXPECT_GT(mark.width, 0);
          EXPECT_LT(mark.x + mark.width, 224);
          EXPECT_GE(mark.y, 0);
          EXPECT_LT(mark.y + mark.height, height);
          EXPECT_EQ(mark.latestDay, h == 20);
          EXPECT_EQ(mark.known, h == 20);
        }
        for (unsigned c = 0; c < 5; ++c) {
          ASSERT_TRUE(widgets::Records::recordCell(card, {456, height, 18, false, touch}, c, mark));
          EXPECT_GE(mark.x, 10);
          EXPECT_LE(mark.x + mark.width, 446);
          EXPECT_LT(mark.y + mark.height, height);
        }
      }
  EXPECT_FALSE(widgets::When::hourColumn(card, hours, {224, 108, 18, true, false}, 0, mark));
  EXPECT_FALSE(widgets::Records::recordCell(card, {224, 108, 18, false, false}, 0, mark));
  EXPECT_FALSE(widgets::When::hourColumn(card, hours, {224, 108, 18, false, false}, 24, mark));
  EXPECT_FALSE(widgets::Records::recordCell(card, {456, 108, 18, false, false}, 5, mark));
  card.bytes++;
  EXPECT_FALSE(widgets::When::hourColumn(card, hours, {224, 108, 18, false, false}, 0, mark));
  EXPECT_FALSE(widgets::Records::recordCell(card, {456, 108, 18, false, false}, 0, mark));
}
