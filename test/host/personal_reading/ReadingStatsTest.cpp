#include <gtest/gtest.h>

#include <vector>

#include "booksync/services/ReadingStats.h"
using namespace booksync;
namespace {
constexpr uint64_t Now = 200 * 86400 + 1000;
struct Visits {
  std::vector<storage::PageEvent> events;
  size_t cursor = 0;
  bool fail = false;
  static StatsStep next(void* p, storage::PageEvent& out) {
    auto& v = *static_cast<Visits*>(p);
    if (v.fail) return StatsStep::Error;
    if (v.cursor == v.events.size()) return StatsStep::End;
    out = v.events[v.cursor++];
    return StatsStep::Record;
  }
  StatsSource source() { return {next, this, {7, 2}, Now - 86400, Now, true}; }
  void add(uint64_t start, uint32_t seconds) { events.push_back({1, start, uint32_t(events.size() + 1), seconds}); }
};
void finish(ReadingStats& stats) {
  for (unsigned i = 0; stats.busy() && i < 30000; ++i) ASSERT_TRUE(stats.tick(2));
  ASSERT_FALSE(stats.busy());
}
}  // namespace
TEST(ReadingStats, NoCumulativeOrSessionDoubleCountAndOverlapSafeToday) {
  Visits visits;
  visits.add(Now - 100, 80);
  visits.add(Now - 80, 80);
  ReadingStats stats;
  ASSERT_TRUE(stats.begin(visits.source(), Now, 0, {Now - 101, true}));
  finish(stats);
  EXPECT_EQ(stats.local().today.value, 100);
  EXPECT_EQ(stats.local().pendingToday.value, 100);
  EXPECT_EQ(stats.local().recordedPages.value, 2);
  EXPECT_EQ(stats.local().longestSession.value, 100);
}
TEST(ReadingStats, ServerFirstZeroAndExactSnapshotWatermark) {
  Visits visits;
  visits.add(Now - 100, 20);
  visits.add(Now - 50, 30);
  ReadingStats stats;
  ASSERT_TRUE(stats.begin(visits.source(), Now, 0, {Now - 75, true}));
  finish(stats);
  RemoteReadingStats remote;
  remote.scope = {7, 2};
  remote.daysAvailable = true;
  remote.firstDay = 200;
  remote.dayCount = 1;
  remote.days[0] = {0, true};
  remote.includedThrough = {Now - 75, true};
  remote.streak = {0, true};
  remote.finished = {0, true};
  remote.goalAvailable = true;
  remote.target = {0, true};
  StatsSnapshot out;
  stats.combine(&remote, {7, 2}, true, 200, out);
  EXPECT_EQ(out.today.value, 30);
  EXPECT_EQ(out.todayMerge, TodayMerge::Watermarked);
  EXPECT_EQ(out.daysCoverage.origin, ReadingOrigin::Account);
  EXPECT_EQ(out.streak.value, 0);
  EXPECT_TRUE(out.finished.known);
  EXPECT_TRUE(out.target.known);
  remote.includedThrough.value++;
  stats.combine(&remote, {7, 2}, true, 200, out);
  EXPECT_EQ(out.today.value, 0);
  EXPECT_EQ(out.localToday.value, 50);
  EXPECT_EQ(out.todayMerge, TodayMerge::Separate);
  remote.includedThrough = {Now + 1, true};
  stats.combine(&remote, {7, 2}, true, 200, out);
  EXPECT_EQ(out.todayMerge, TodayMerge::Separate);
}
TEST(ReadingStats, AbsentIsNotZeroAndAccountsCannotLeak) {
  ReadingStats stats;
  StatsSnapshot out;
  RemoteReadingStats remote;
  remote.scope = {8, 1};
  remote.streak = {99, true};
  stats.combine(&remote, {7, 2}, true, 200, out);
  EXPECT_FALSE(out.today.known);
  EXPECT_FALSE(out.streak.known);
  EXPECT_FALSE(out.finished.known);
  Visits visits;
  ASSERT_TRUE(stats.begin(visits.source(), Now, 0, {}));
  finish(stats);
  stats.combine(nullptr, {7, 2}, true, 200, out);
  EXPECT_TRUE(out.today.known);
  EXPECT_EQ(out.today.value, 0);
  EXPECT_EQ(out.daysCoverage.origin, ReadingOrigin::ThisReader);
  EXPECT_TRUE(out.offline);
  EXPECT_FALSE(out.days[0].known);  // before proven capture coverage, never fake old zeroes
}
TEST(ReadingStats, GapsAndClockReversalPreventReconciliation) {
  Visits visits;
  visits.add(Now - 100, 20);
  visits.events.push_back({0, Now - 90, 0, 0, 0, 0, 0, 0, 0, 0, storage::PageEvent::Gap});
  ReadingStats stats;
  ASSERT_TRUE(stats.begin(visits.source(), Now, 0, {Now - 200, true}));
  finish(stats);
  EXPECT_FALSE(stats.local().pendingToday.known);
  EXPECT_FALSE(stats.local().coverage.complete);
  visits.events.clear();
  visits.cursor = 0;
  visits.add(Now - 100, 20);
  visits.add(Now - 200, 20);
  ASSERT_TRUE(stats.begin(visits.source(), Now, 0, {}));
  finish(stats);
  EXPECT_FALSE(stats.local().coverage.complete);
}
TEST(ReadingStats, MidnightClippingAndInclusiveStartWatermark) {
  Visits visits;
  visits.add(200 * 86400 - 10, 20);
  visits.add(Now - 20, 10);
  ReadingStats stats;
  ASSERT_TRUE(stats.begin(visits.source(), Now, 0, {Now - 20, true}));
  finish(stats);
  EXPECT_EQ(stats.local().today.value, 20);
  EXPECT_EQ(stats.local().pendingToday.value, 0);
  EXPECT_EQ(stats.local().days[ReadingStats::Days - 2].value, 10);
}
TEST(ReadingStats, ErrorsDoNotPublishPartialHistoryAndWorkIsBounded) {
  Visits visits;
  for (unsigned i = 0; i < 50000; ++i) visits.add(Now - 60000 + i, 1);
  ReadingStats stats;
  ASSERT_TRUE(stats.begin(visits.source(), Now, 0, {}));
  ASSERT_TRUE(stats.tick());
  EXPECT_LE(visits.cursor, ReadingStats::Batch);
  visits.fail = true;
  EXPECT_FALSE(stats.tick());
  EXPECT_FALSE(stats.local().today.known);
}
TEST(ReadingStats, ServerOnlyHistoryUsesRequestedDayNotLastSavedDay) {
  ReadingStats stats;
  RemoteReadingStats remote;
  StatsSnapshot out;
  remote.scope = {7, 2};
  remote.daysAvailable = true;
  remote.firstDay = 199;
  remote.dayCount = 1;
  remote.days[0] = {60, true};
  remote.streak = {0, true};
  stats.combine(&remote, {7, 2}, true, 200, out);
  EXPECT_FALSE(out.today.known);
  EXPECT_EQ(out.days[ReadingStats::Days - 2].value, 60);
  EXPECT_EQ(out.daysCoverage.origin, ReadingOrigin::Account);
  EXPECT_TRUE(out.streak.known);
  EXPECT_FALSE(out.recordedPages.known);
  EXPECT_FALSE(out.finished.known);
}
TEST(ReadingStats, ServerMissingFieldsRetainLocalWhileAccountGoalNeverInvented) {
  Visits visits;
  visits.add(Now - 100, 20);
  ReadingStats stats;
  StatsSnapshot out;
  RemoteReadingStats remote;
  remote.scope = {7, 2};
  ASSERT_TRUE(stats.begin(visits.source(), Now, 0, {}));
  finish(stats);
  stats.combine(&remote, {7, 2}, false, 200, out);
  EXPECT_TRUE(out.today.known);
  EXPECT_EQ(out.today.value, 20);
  EXPECT_FALSE(out.target.known);
  EXPECT_FALSE(out.finished.known);
  EXPECT_TRUE(out.recordedPages.known);
  EXPECT_FALSE(out.offline);
}
TEST(ReadingStats, NativeUnboundHistoryStaysSeparateFromAccount) {
  Visits visits;
  visits.add(Now - 100, 20);
  auto source = visits.source();
  source.scope = {};
  ReadingStats stats;
  RemoteReadingStats remote;
  StatsSnapshot out;
  ASSERT_TRUE(stats.begin(source, Now, 0, {Now - 101, true}));
  finish(stats);
  remote.scope = {7, 2};
  remote.daysAvailable = true;
  remote.firstDay = 200;
  remote.dayCount = 1;
  remote.days[0] = {0, true};
  remote.includedThrough = {Now - 101, true};
  stats.combine(&remote, {7, 2}, true, 200, out);
  EXPECT_EQ(out.todayMerge, TodayMerge::Separate);
  EXPECT_EQ(out.today.value, 0);
  EXPECT_EQ(out.localToday.value, 20);
}
TEST(ReadingStats, UnknownRemoteDayRemainsUnknownDespiteWatermark) {
  Visits visits;
  visits.add(Now - 100, 20);
  ReadingStats stats;
  RemoteReadingStats remote;
  StatsSnapshot out;
  ASSERT_TRUE(stats.begin(visits.source(), Now, 0, {Now - 101, true}));
  finish(stats);
  remote.scope = {7, 2};
  remote.daysAvailable = true;
  remote.firstDay = 200;
  remote.dayCount = 1;
  remote.includedThrough = {Now - 101, true};
  stats.combine(&remote, {7, 2}, true, 200, out);
  EXPECT_FALSE(out.today.known);
  EXPECT_EQ(out.todayMerge, TodayMerge::Separate);
}
TEST(ReadingStats, FiftyThousandEventsStreamWithoutHistorySizedStorage) {
  Visits visits;
  for (unsigned i = 0; i < 50000; ++i) visits.add(Now - 60000 + i, 1);
  ReadingStats stats;
  ASSERT_TRUE(stats.begin(visits.source(), Now, 0, {}));
  finish(stats);
  EXPECT_EQ(stats.local().recordedPages.value, 50000);
  EXPECT_EQ(stats.local().longestSession.value, 50000);
}
TEST(ReadingStats, DeviceCalendarOwnsHistoricalDayBoundaries) {
  // Synthetic DST-like short day: day 199 ends one hour early.
  auto split = [](void*, uint64_t t, StatsDaySlice& out) -> bool {
    const uint64_t boundary = 200 * 86400 - 3600;
    out = t < boundary ? StatsDaySlice{199, boundary} : StatsDaySlice{200, 201 * 86400 - 3600};
    return true;
  };
  Visits visits;
  visits.add(200 * 86400 - 3610, 20);
  ReadingStats stats;
  ASSERT_TRUE(stats.begin(visits.source(), Now, 0, {}, {split, nullptr}));
  finish(stats);
  EXPECT_EQ(stats.local().today.value, 10);
  EXPECT_EQ(stats.local().days[ReadingStats::Days - 2].value, 10);
}
TEST(ReadingStats, CoverageGapRevokesKnownEmptyDays) {
  Visits visits;
  visits.events.push_back({0, Now - 90, 0, 0, 0, 0, 0, 0, 0, 0, storage::PageEvent::Gap});
  ReadingStats stats;
  ASSERT_TRUE(stats.begin(visits.source(), Now, 0, {Now - 101, true}));
  finish(stats);
  EXPECT_FALSE(stats.local().today.known);
  EXPECT_FALSE(stats.local().pendingToday.known);
  EXPECT_FALSE(stats.local().coverage.complete);
}

TEST(ReadingStats, IncompleteCaptureCannotPublishPendingProof) {
  Visits visits;
  visits.add(Now - 100, 20);
  auto source = visits.source();
  source.complete = false;
  ReadingStats stats;
  ASSERT_TRUE(stats.begin(source, Now, 0, {Now - 101, true}));
  finish(stats);
  EXPECT_FALSE(stats.local().pendingToday.known);
}

TEST(ReadingStats, DurablePageCursorUsesValidHistoryAndFrozenRowCount) {
  constexpr int64_t day = 20000;
  constexpr uint64_t now = day * 86400 + 1000;
  card = {};
  storage::PageJournal journal{"/personal/pages"};
  ASSERT_EQ(journal.recover(), storage::JournalResult::Ok);
  ASSERT_EQ(journal.append({1, now - 100, 1, 80, 0, 0, 100, 0, 1, 100}), storage::JournalResult::Ok);
  ASSERT_EQ(journal.append({1, now - 80, 2, 80, 0, 1, 100, 0, 2, 100}), storage::JournalResult::Ok);
  card.restart();
  storage::PageJournal reboot{"/personal/pages"};
  ASSERT_EQ(reboot.recover(), storage::JournalResult::Ok);
  PageStatsSource cursor(reboot);
  auto source = cursor.freeze({7, 2}, now - 86400, now, true);
  // A later append must not enter the frozen view or revoke its coverage.
  ASSERT_EQ(reboot.append({2, now - 1000, 1, 10, 0, 0, 10, 0, 1, 10}), storage::JournalResult::Ok);
  ReadingStats stats;
  ASSERT_TRUE(stats.begin(source, now, 0, {now - 101, true}));
  finish(stats);
  EXPECT_EQ(stats.local().recordedPages.value, 2);
  EXPECT_EQ(stats.local().today.value, 100);
  EXPECT_TRUE(stats.local().coverage.complete);
  EXPECT_EQ(stats.local().pendingToday.value, 100);
  EXPECT_EQ(card.handles, 0);
}
TEST(ReadingStats, StreakUsesWholeCapturedHistoryBeyondCalendarWindow) {
  constexpr int64_t day = 24000;
  constexpr uint64_t now = day * 86400 + 1000;
  Visits visits;
  visits.events.reserve(400);
  for (int64_t n = day - 399; n <= day; ++n) visits.add(n * 86400 + 500, 60);
  auto source = visits.source();
  source.since = (day - 400) * 86400;
  source.through = now;
  ReadingStats stats;
  ASSERT_TRUE(stats.begin(source, now, 0, {}));
  finish(stats);
  EXPECT_TRUE(stats.local().streak.known);
  EXPECT_EQ(stats.local().streak.value, 400);
  EXPECT_EQ(stats.local().bestStreak.value, 400);
}
