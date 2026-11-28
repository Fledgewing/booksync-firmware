#include <gtest/gtest.h>

#include <cstring>
#include <memory>

#include "booksync/services/SeriesPlan.h"
using namespace booksync;
namespace {
struct Fixture : testing::Test {
  QueryState state{{1, 1, 7, 1}, 12, QueryAllFields, true};
  uint64_t revision = 1;
  uint32_t reads = 0, active = 0xffffffffu, unknown = 0xffffffffu;
  uint32_t fail = 0xffffffffu, current = 3, finished = 1;
  bool reading = true, partial = false, secondary = false, nulls = false;
  bool ties = false, missingDate = false, malformed = false;
  uint32_t extraReading = 0xffffffffu;
  bool local = false, unknownDevice = false, change = false;
  SeriesRequest request{};
  SeriesPlanView result{};
  std::unique_ptr<SeriesPlan> plan;
  void SetUp() override {
    std::strcpy(request.key.data(), "series:opaque");
    request.requestId = 9;
    plan = std::make_unique<SeriesPlan>(
        QuerySource{this, [](void* p) { return static_cast<Fixture*>(p)->state; },
                    [](void* p, BookKey key, QueryRecord& out) {
                      auto& f = *static_cast<Fixture*>(p);
                      ++f.reads;
                      if (key.ordinal == f.fail) return false;
                      out.clear();
                      out.book.key = key;
                      std::snprintf(out.book.title.data(), out.book.title.size(), "Book %u", key.ordinal);
                      out.seriesCount = f.malformed ? 9 : f.secondary ? 2 : 1;
                      const unsigned i = f.secondary ? 1 : 0;
                      std::strcpy(out.seriesKeys[0].data(), "other:primary");
                      std::strcpy(out.seriesKeys[i].data(), "series:opaque");
                      std::strcpy(out.series[i].data(), "Example series");
                      out.seriesPositions[i] = key.ordinal / 2.0;
                      out.seriesPositionsKnown[i] = !f.nulls || key.ordinal < 5;
                      out.seriesRanks[i] = f.ties && key.ordinal == 4 ? 20 : key.ordinal;
                      if (f.ties && key.ordinal == 5) {
                        out.seriesPositions[i] = 2.0;
                        out.seriesRanks[i] = 10;
                      }
                      out.authorCount = 1;
                      std::strcpy(out.authors[0].data(), "Example Press");
                      out.status = (key.ordinal == f.current || key.ordinal == f.extraReading) && f.reading
                                       ? QueryShow::Reading
                                   : f.finished != 0xffffffffu && key.ordinal <= f.finished ? QueryShow::Finished
                                                                                            : QueryShow::Unread;
                      out.onDevice = f.unknownDevice ? QueryTruth::Unknown : f.local ? QueryTruth::Yes : QueryTruth::No;
                      if (!f.missingDate || key.ordinal != f.current) std::strcpy(out.last.data(), "2026-11-28");
                      return true;
                    }},
        SeriesJobsView{this, [](void* p) { return static_cast<Fixture*>(p)->revision; },
                       [](void* p, BookKey key) {
                         auto& f = *static_cast<Fixture*>(p);
                         return key.ordinal == f.unknown  ? QueryTruth::Unknown
                                : key.ordinal == f.active ? QueryTruth::Yes
                                                          : QueryTruth::No;
                       }});
  }
  void run() {
    ASSERT_TRUE(plan->request(request));
    unsigned ticks = 0;
    while (plan->busy() && ticks++ < 4000) {
      plan->tick(7);
      EXPECT_LE(plan->lastWork(), 7);
    }
    ASSERT_FALSE(plan->busy());
    plan->copyPlan(result);
  }
};
TEST_F(Fixture, DefaultThreeFollowCurrentReading) {
  run();
  ASSERT_EQ(result.count, 3);
  EXPECT_EQ(result.next[0].book.key.ordinal, 4);
  EXPECT_EQ(result.total, 12);
  EXPECT_EQ(result.read, 2);
  EXPECT_EQ(result.reading, 1);
  EXPECT_TRUE(result.earlierKnown);
  EXPECT_EQ(result.earlier.book.key.ordinal, 2);
  EXPECT_NE(result.token, 0);
  EXPECT_LE(reads, 24);
}
TEST_F(Fixture, LastFinishedFallback) {
  reading = false;
  run();
  EXPECT_EQ(result.next[0].book.key.ordinal, 2);
}
TEST_F(Fixture, BeginningWhenNoAnchor) {
  reading = false;
  finished = 0xffffffffu;
  run();
  EXPECT_EQ(result.next[0].book.key.ordinal, 0);
  EXPECT_FALSE(result.earlierKnown);
}
TEST_F(Fixture, ExplicitCurrentWins) {
  request.currentKnown = true;
  request.current = {state.provenance, 8};
  run();
  EXPECT_EQ(result.next[0].book.key.ordinal, 9);
}
TEST_F(Fixture, ExplicitGapIncludesStartingRow) {
  request.startKnown = true;
  request.start = {state.provenance, 2};
  run();
  EXPECT_EQ(result.next[0].book.key.ordinal, 2);
  EXPECT_FALSE(result.earlierKnown);
}
TEST_F(Fixture, NoWrappingAfterLastBook) {
  request.currentKnown = true;
  request.current = {state.provenance, 11};
  run();
  EXPECT_EQ(result.count, 0);
  EXPECT_TRUE(result.earlierKnown);
}
TEST_F(Fixture, SkipsActiveAndUnknownJobs) {
  active = 4;
  unknown = 5;
  run();
  EXPECT_EQ(result.next[0].book.key.ordinal, 6);
}
TEST_F(Fixture, UnknownDeviceIsNotServerOnly) {
  unknownDevice = true;
  run();
  EXPECT_EQ(result.count, 0);
  EXPECT_FALSE(result.deviceKnown);
}
TEST_F(Fixture, AllRemainingSaved) {
  local = true;
  run();
  EXPECT_EQ(result.count, 0);
  EXPECT_TRUE(result.remainingSaved);
}
TEST_F(Fixture, SecondaryMembershipAndDecimalOrder) {
  secondary = true;
  nulls = true;
  request.count = 10;
  run();
  ASSERT_EQ(result.count, 8);
  EXPECT_DOUBLE_EQ(result.next[0].position, 2.0);
  EXPECT_EQ(result.next[7].book.key.ordinal, 11);
}
TEST_F(Fixture, CountBoundsRejectWithoutReads) {
  request.count = 0;
  EXPECT_FALSE(plan->request(request));
  request.count = 11;
  EXPECT_FALSE(plan->request(request));
  EXPECT_EQ(reads, 0);
}
TEST_F(Fixture, IncompleteMembershipDoesNotInventSelection) {
  state.fields &= ~queryField(QueryField::SeriesIdentity);
  run();
  EXPECT_EQ(result.availability, Availability::Unsupported);
  EXPECT_TRUE(result.membershipIncomplete);
  EXPECT_EQ(result.count, 0);
  EXPECT_EQ(result.token, 0);
}
TEST_F(Fixture, CatalogOwnerChangeRetiresPublishedResult) {
  run();
  ++state.provenance.accountEpoch;
  plan->copyPlan(result);
  EXPECT_EQ(result.availability, Availability::Unavailable);
  EXPECT_EQ(result.count, 0);
  EXPECT_EQ(result.token, 0);
}
TEST_F(Fixture, ActiveJobRevisionChangeRetiresInFlightResult) {
  ASSERT_TRUE(plan->request(request));
  plan->tick(1);
  ++revision;
  plan->tick(1);
  plan->copyPlan(result);
  EXPECT_EQ(result.availability, Availability::Unavailable);
  EXPECT_FALSE(plan->busy());
}
TEST_F(Fixture, ReadFailureDoesNotPublishPartialSelection) {
  fail = 8;
  run();
  EXPECT_EQ(result.availability, Availability::Unavailable);
  EXPECT_EQ(result.count, 0);
}
TEST_F(Fixture, TenThousandRowsRemainBounded) {
  state.count = 10000;
  request.count = 10;
  run();
  EXPECT_EQ(result.total, 10000);
  EXPECT_EQ(result.count, 10);
  EXPECT_EQ(reads, 20000);
}
TEST_F(Fixture, PreviewTokenStableUntilRelevantChange) {
  run();
  const auto token = result.token;
  reads = 0;
  run();
  EXPECT_EQ(token, result.token);
  ++revision;
  active = 4;
  run();
  EXPECT_NE(token, result.token);
}
TEST_F(Fixture, SnapshotCopyDoesNotScanCatalog) {
  run();
  const auto before = reads;
  for (unsigned i = 0; i < 100; ++i) plan->copyPlan(result);
  EXPECT_EQ(before, reads);
}
TEST_F(Fixture, EmptyMembershipIsKnownEmpty) {
  state.count = 0;
  run();
  EXPECT_EQ(result.total, 0u);
  EXPECT_EQ(result.count, 0u);
  EXPECT_EQ(result.availability, Availability::Ready);
  EXPECT_FALSE(result.remainingSaved);
}
TEST_F(Fixture, MissingSavedCatalogDoesNoScan) {
  state.ready = false;
  run();
  EXPECT_EQ(reads, 0u);
  EXPECT_EQ(result.availability, Availability::NotSaved);
}
TEST_F(Fixture, MostRecentReadingTieUsesStableKey) {
  extraReading = 8;
  run();
  EXPECT_EQ(result.next[0].book.key.ordinal, 9u);
}
TEST_F(Fixture, MissingReadingDateMatchesAndroidNullLastComparator) {
  extraReading = 8;
  missingDate = true;
  run();
  EXPECT_EQ(result.next[0].book.key.ordinal, 4u);
}
TEST_F(Fixture, EqualContextualIndexesRetainSourceRank) {
  ties = true;
  run();
  ASSERT_EQ(result.count, 3);
  EXPECT_EQ(result.next[0].book.key.ordinal, 5u);
  EXPECT_EQ(result.next[1].book.key.ordinal, 4u);
}
TEST_F(Fixture, MissingExplicitStartFailsClosed) {
  request.startKnown = true;
  request.start = {state.provenance, 999};
  run();
  EXPECT_EQ(result.availability, Availability::Unavailable);
  EXPECT_EQ(result.count, 0u);
}
TEST_F(Fixture, ForeignStartingKeyDoesNotScan) {
  request.startKnown = true;
  request.start = {state.provenance, 2};
  ++request.start.provenance.accountEpoch;
  run();
  EXPECT_EQ(reads, 0u);
  EXPECT_EQ(result.availability, Availability::Unavailable);
}
TEST_F(Fixture, MalformedMembershipRetainsNoPartialData) {
  malformed = true;
  run();
  EXPECT_EQ(result.availability, Availability::Unavailable);
  EXPECT_EQ(result.total, 0u);
}
TEST_F(Fixture, PendingSnapshotHidesPartiallySelectedRows) {
  ASSERT_TRUE(plan->request(request));
  plan->tick(1);
  plan->copyPlan(result);
  EXPECT_TRUE(result.pending);
  EXPECT_EQ(result.count, 0u);
  EXPECT_EQ(result.token, 0u);
}
TEST_F(Fixture, CancelRetiresProjectionWithoutChangingJobs) {
  ASSERT_TRUE(plan->request(request));
  plan->tick(1);
  plan->cancel();
  plan->copyPlan(result);
  EXPECT_EQ(result.availability, Availability::Unavailable);
  EXPECT_EQ(revision, 1u);
  EXPECT_EQ(reads, 1u);
}
TEST_F(Fixture, ZeroAllowanceDoesNotConsumeSource) {
  ASSERT_TRUE(plan->request(request));
  plan->tick(0);
  EXPECT_TRUE(plan->busy());
  EXPECT_EQ(reads, 0u);
}
TEST_F(Fixture, PreviewTokenIgnoresBytesAfterStringTerminator) {
  run();
  const auto token = result.token;
  request.key.back() = 'x';
  run();
  EXPECT_EQ(token, result.token);
}
TEST_F(Fixture, PendingSnapshotRetainsRequestedCount) {
  request.count = 10;
  ASSERT_TRUE(plan->request(request));
  plan->tick(1);
  plan->copyPlan(result);
  EXPECT_TRUE(result.pending);
  EXPECT_EQ(result.requested, 10);
}
}  // namespace
