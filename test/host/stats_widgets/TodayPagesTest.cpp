// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "booksync/services/widgets/Pages.h"
#include "booksync/services/widgets/Today.h"
#include "booksync/ui/widgets/Pages.h"
#include "booksync/ui/widgets/Today.h"
using namespace booksync;
namespace {
using SummaryProject = void (*)(const StatsSnapshot&, const StatsProjection&, StatsCard&);
class TodayPagesStates : public testing::TestWithParam<SummaryProject> {
 protected:
  StatsSnapshot stats;
  StatsProjection options;
  StatsCard card;
  void SetUp() override {
    stats.nativeToday = {1440, true};
    stats.recordedPages = {1240, true};
    stats.todayPages = {36, true};
    stats.nativeTodayCoverage = stats.recordsCoverage = {ReadingOrigin::ThisReader, {}, 1, 100, true, true};
  }
  void project() { GetParam()(stats, options, card); }
};
TEST_P(TodayPagesStates, Known) {
  project();
  EXPECT_EQ(card.state, StatsCardState::Ready);
  EXPECT_TRUE(card.value.known);
}
TEST_P(TodayPagesStates, Empty) {
  stats.nativeToday.value = stats.recordedPages.value = stats.todayPages.value = 0;
  project();
  EXPECT_EQ(card.state, StatsCardState::Empty);
}
TEST_P(TodayPagesStates, NotSaved) {
  stats.nativeToday = stats.recordedPages = stats.todayPages = {};
  project();
  EXPECT_EQ(card.state, StatsCardState::NotSaved);
}
TEST_P(TodayPagesStates, Off) {
  options.enabled = false;
  project();
  EXPECT_EQ(card.state, StatsCardState::Off);
}
TEST_P(TodayPagesStates, Unsupported) {
  options.availability = StatsCardState::Unsupported;
  project();
  EXPECT_EQ(card.state, StatsCardState::Unsupported);
}
TEST_P(TodayPagesStates, Stale) {
  options.stale = true;
  options.ageDays = 2;
  project();
  EXPECT_EQ(card.state, StatsCardState::Ready);
  EXPECT_TRUE(card.stale);
  EXPECT_EQ(card.ageDays, 2);
}
TEST_P(TodayPagesStates, Oversize) {
  options.availability = StatsCardState::Oversize;
  project();
  EXPECT_EQ(card.state, StatsCardState::Oversize);
}
TEST_P(TodayPagesStates, Error) {
  options.availability = StatsCardState::Unavailable;
  project();
  EXPECT_EQ(card.state, StatsCardState::Unavailable);
}
TEST_P(TodayPagesStates, RetiredAccount) {
  stats.nativeTodayCoverage = stats.accountTodayCoverage =
      stats.recordsCoverage = {ReadingOrigin::Account, {7, 2}, 1, 100, true, true};
  options.provenance = {};
  options.provenance.owner = 7;
  options.provenance.accountEpoch = 3;
  project();
  EXPECT_EQ(card.state, StatsCardState::NotSaved);
}
INSTANTIATE_TEST_SUITE_P(TodayAndPages, TodayPagesStates, testing::Values(projectToday, projectPages));
TEST(StatsToday, NativeAndAccountRemainSeparateIncludingZero) {
  StatsSnapshot stats;
  StatsProjection options;
  StatsCard card;
  options.provenance.owner = 7;
  options.provenance.accountEpoch = 2;
  stats.nativeToday = {1440, true};
  stats.nativeTodayCoverage = {ReadingOrigin::ThisReader, {}, 1, 100, true, true};
  stats.accountToday = {0, true};
  stats.accountTodayCoverage = {ReadingOrigin::Account, {7, 2}, 1, 100, true, true};
  projectToday(stats, options, card);
  EXPECT_EQ(card.value.value, 24);
  EXPECT_EQ(card.reader.value, 24);
  EXPECT_TRUE(card.account.known);
  EXPECT_EQ(card.account.value, 0);
  EXPECT_TRUE(card.accountLinked);
  options.provenance.accountEpoch = 3;
  projectToday(stats, options, card);
  EXPECT_EQ(card.value.value, 24);
  EXPECT_FALSE(card.account.known);
  EXPECT_FALSE(card.accountLinked);
}
TEST(StatsPages, UnknownTodayDoesNotBecomeZeroFromLifetime) {
  StatsSnapshot stats;
  StatsProjection options;
  StatsCard card;
  stats.recordedPages = {1240, true};
  projectPages(stats, options, card);
  EXPECT_EQ(card.state, StatsCardState::Ready);
  EXPECT_FALSE(card.value.known);
  EXPECT_EQ(card.best.value, 1240);
}
}  // namespace

TEST(TodayPagesGeometry, BothProfilesAndBoundedSizes) {
  StatsCard statsValue;
  statsValue.state = StatsCardState::Ready;
  widgets::StatsSummary positions;
  for (auto layout : {widgets::Today::summaryPositions, widgets::Pages::summaryPositions})
    for (bool touch : {false, true})
      for (int height : {80, 96, 108}) {
        for (bool stale : {false, true}) {
          statsValue.stale = stale;
          ASSERT_TRUE(layout(statsValue, {224, height, 18, false, touch}, positions));
          EXPECT_GE(positions.numberY, 0);
          EXPECT_GT(positions.captionY, positions.numberY);
          EXPECT_GT(positions.sourceY, positions.captionY);
          EXPECT_LT(positions.footerY, height);
        }
        EXPECT_FALSE(layout(statsValue, {224, height, 18, true, touch}, positions));
        statsValue.bytes++;
        EXPECT_FALSE(layout(statsValue, {224, height, 18, false, touch}, positions));
        statsValue.bytes--;
      }
}
TEST(StatsToday, AccountOnlyIsKnownWithoutInventingNativeZero) {
  StatsSnapshot stats;
  StatsProjection options;
  StatsCard out;
  options.provenance.owner = 7;
  options.provenance.accountEpoch = 2;
  stats.accountToday = {720, true};
  stats.accountTodayCoverage = {ReadingOrigin::Account, {7, 2}, 1, 100, true, true};
  projectToday(stats, options, out);
  EXPECT_EQ(out.state, StatsCardState::Ready);
  EXPECT_EQ(out.source, StatsCardSource::Account);
  EXPECT_EQ(out.value.value, 12);
  EXPECT_FALSE(out.reader.known);
  EXPECT_TRUE(out.accountLinked);
  options.provenance.accountEpoch = 3;
  projectToday(stats, options, out);
  EXPECT_EQ(out.state, StatsCardState::NotSaved);
  EXPECT_FALSE(out.account.known);
}

TEST(StatsToday, ReaderPrimaryRetainsAccountSecondaryAge) {
  StatsSnapshot stats;
  StatsProjection options;
  StatsCard out;
  options.provenance.owner = 7;
  options.provenance.accountEpoch = 2;
  options.now = 200000;
  stats.nativeToday = {1440, true};
  stats.nativeTodayCoverage = {ReadingOrigin::ThisReader, {}, 1, 200000, true, true};
  stats.accountToday = {720, true};
  stats.accountTodayCoverage = {ReadingOrigin::Account, {7, 2}, 1, 20000, true, true};
  projectToday(stats, options, out);
  EXPECT_EQ(out.value.value, 24);
  EXPECT_EQ(out.account.value, 12);
  EXPECT_TRUE(out.stale);
  EXPECT_EQ(out.ageDays, 2);
  options.provenance.accountEpoch = 3;
  projectToday(stats, options, out);
  EXPECT_EQ(out.value.value, 24);
  EXPECT_FALSE(out.account.known);
  EXPECT_FALSE(out.stale);
}
