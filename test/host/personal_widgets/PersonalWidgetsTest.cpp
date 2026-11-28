#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "booksync/services/widgets/PersonalWidgets.h"
using namespace booksync;
namespace {
struct Source final : PersonalReadingSource {
  std::vector<std::string> ids{"alpha", "beta", "gamma", "delta", "epsilon"};
  PersonalState state = PersonalState::Ready;
  ReadingScope scope{};
  uint32_t reads = 0;
  bool duplicateKeys = false, invalidTitle = false;
  ReadingScope owner() const override { return scope; }
  PersonalState availability(PersonalKind) const override { return state; }
  PersonalStep read(PersonalKind, uint32_t ordinal, PersonalRecord& out) override {
    ++reads;
    if (ordinal >= ids.size()) return PersonalStep::End;
    out = {};
    std::strcpy(out.identity.data(), ids[ordinal].c_str());
    out.book.key.provenance = {9, scope.epoch, scope.owner, 3};
    out.book.key.ordinal = duplicateKeys ? 0 : ordinal;
    std::strcpy(out.book.title.data(), ids[ordinal].c_str());
    if (invalidTitle) out.book.title.back() = 'x';
    return PersonalStep::Record;
  }
  PersonalStep find(PersonalKind, std::string_view id, uint32_t& ordinal) override {
    for (uint32_t i = 0; i < ids.size(); ++i)
      if (ids[i] == id) {
        ordinal = i;
        return PersonalStep::Record;
      }
    return PersonalStep::End;
  }
};
struct Fixture : testing::Test {
  Source source;
  PersonalReadingSource* sources[1]{&source};
  std::unique_ptr<PersonalReading> personal = std::make_unique<PersonalReading>(sources, 1);
  PersonalWidgetValues out{};
  DiscoverProjection discover{*personal, out};
  PersonalWidgetCapture capture{{9, 1, 0, 3}, {}, false, false, 0};
  void run() {
    for (unsigned i = 0; discover.busy() && i < 100000; ++i) {
      const auto before = source.reads;
      ASSERT_TRUE(discover.tick(capture, 7));
      ASSERT_LE(source.reads - before, 7u);
      if (discover.busy()) EXPECT_EQ(out.count, 0);
    }
    ASSERT_FALSE(discover.busy());
  }
};
}  // namespace
TEST_F(Fixture, FourRowsAreExplicitlyChargedAndSelectedByF39Rank) {
  ASSERT_TRUE(discover.begin(200, 0, capture, {4, LibraryBytes}));
  run();
  ASSERT_EQ(out.state, PersonalWidgetState::Ready);
  ASSERT_EQ(out.count, 4);
  EXPECT_EQ(out.total, 5);
  EXPECT_EQ(out.rows, 4);
  EXPECT_GE(out.bytes, sizeof(BookRow) * 4);
  const char* expected[]{"beta", "delta", "epsilon", "alpha"};
  // Independent SHA-256 order for day 200, not an order produced by the renderer.
  for (unsigned i = 0; i < 4; ++i) EXPECT_STREQ(out.books[i].title.data(), expected[i]);
}
TEST_F(Fixture, OldSinglePickAllowanceCannotHideFourBooks) {
  EXPECT_FALSE(discover.begin(200, 0, capture, {1, LibraryBytes}));
  EXPECT_EQ(out.state, PersonalWidgetState::Oversize);
  EXPECT_EQ(source.reads, 0);
  EXPECT_FALSE(discover.begin(200, 0, capture, {4, uint32_t(sizeof(BookRow))}));
  EXPECT_EQ(source.reads, 0);
}
TEST_F(Fixture, SmallerPoolHasNoInventedOrRepeatedCovers) {
  source.ids = {"alpha", "beta"};
  ASSERT_TRUE(discover.begin(200, 0, capture, {4, LibraryBytes}));
  run();
  ASSERT_EQ(out.count, 2);
  EXPECT_EQ(out.total, 2);
  EXPECT_FALSE(out.books[0].key == out.books[1].key);
  EXPECT_EQ(out.rows, 2);
}
TEST_F(Fixture, ShuffleAdvancesOneF39CursorAndRestartIsDeterministic) {
  ASSERT_TRUE(discover.begin(200, 0, capture, {4, LibraryBytes}));
  run();
  auto first = out.books[0].key;
  ASSERT_TRUE(discover.begin(200, 1, capture, {4, LibraryBytes}));
  run();
  EXPECT_FALSE(first == out.books[0].key);
  ASSERT_TRUE(discover.begin(200, 0, capture, {4, LibraryBytes}));
  run();
  EXPECT_TRUE(first == out.books[0].key);
}
TEST_F(Fixture, RetirementClearsEveryPendingAndPublishedBook) {
  ASSERT_TRUE(discover.begin(200, 0, capture, {4, LibraryBytes}));
  ASSERT_TRUE(discover.tick(capture, 1));
  ++capture.provenance.generation;
  ASSERT_TRUE(discover.tick(capture));
  EXPECT_FALSE(discover.busy());
  EXPECT_EQ(out.count, 0);
  EXPECT_EQ(out.state, PersonalWidgetState::NotSaved);
  for (const auto& book : out.books) EXPECT_EQ(book.title[0], 0);
}
TEST_F(Fixture, StaleAgeRequiresKnownEvidence) {
  capture.stale = true;
  capture.ageDays = 5;
  ASSERT_TRUE(discover.begin(200, 0, capture, {4, LibraryBytes}));
  run();
  EXPECT_TRUE(out.stale);
  EXPECT_FALSE(out.ageKnown);
  EXPECT_EQ(out.ageDays, 0);
  capture.ageKnown = true;
  ASSERT_TRUE(discover.begin(200, 0, capture, {4, LibraryBytes}));
  run();
  EXPECT_EQ(out.ageDays, 5);
}
TEST_F(Fixture, WrongAccountCannotPublishNativeLookingFallback) {
  source.scope = {7, 2};
  capture.scope = {7, 1};
  ASSERT_TRUE(discover.begin(200, 0, capture, {4, LibraryBytes}));
  run();
  EXPECT_EQ(out.state, PersonalWidgetState::NotSaved);
  EXPECT_EQ(out.count, 0);
}
TEST_F(Fixture, InvalidTickAllowanceCannotConsumeWork) {
  ASSERT_TRUE(discover.begin(200, 0, capture, {4, LibraryBytes}));
  EXPECT_FALSE(discover.tick(capture, 33));
  EXPECT_EQ(source.reads, 0);
}
class States : public Fixture, public testing::WithParamInterface<std::pair<PersonalState, PersonalWidgetState>> {};
TEST_P(States, DiscoverPreservesEachNonReadyStateWithoutInventedRows) {
  source.state = GetParam().first;
  source.ids.clear();
  ASSERT_TRUE(discover.begin(200, 0, capture, {4, LibraryBytes}));
  run();
  EXPECT_EQ(out.state, GetParam().second);
  EXPECT_EQ(out.count, 0);
}
TEST_P(States, RecommendedPreservesEachNonReadyStateAndClearsOldBook) {
  auto pick = std::make_unique<PersonalPick>();
  pick->state = GetParam().first;
  std::strcpy(out.books[0].title.data(), "retired");
  projectRecommendedWidget(*pick, capture, {1, LibraryBytes}, out);
  EXPECT_EQ(out.state, GetParam().second);
  EXPECT_EQ(out.count, 0);
  EXPECT_EQ(out.books[0].title[0], 0);
}
INSTANTIATE_TEST_SUITE_P(IndependentStates, States,
                         testing::Values(std::pair{PersonalState::NotSaved, PersonalWidgetState::NotSaved},
                                         std::pair{PersonalState::Empty, PersonalWidgetState::Empty},
                                         std::pair{PersonalState::Off, PersonalWidgetState::Off},
                                         std::pair{PersonalState::Unsupported, PersonalWidgetState::Unsupported},
                                         std::pair{PersonalState::Unavailable, PersonalWidgetState::Error},
                                         std::pair{PersonalState::Oversized, PersonalWidgetState::Oversize}));
TEST_F(Fixture, RecommendedChargesOneRealBookWithFullCount) {
  auto pick = std::make_unique<PersonalPick>();
  pick->state = PersonalState::Ready;
  pick->count = 9;
  pick->record.book.key.provenance = capture.provenance;
  std::strcpy(pick->record.book.title.data(), "Northbound");
  ASSERT_TRUE(projectRecommendedWidget(*pick, capture, {1, LibraryBytes}, out));
  EXPECT_EQ(out.count, 1);
  EXPECT_EQ(out.rows, 1);
  EXPECT_EQ(out.total, 9);
  EXPECT_STREQ(out.books[0].title.data(), "Northbound");
  EXPECT_EQ(out.books[1].title[0], 0);
}
TEST_F(Fixture, RecommendedRejectsBudgetAndMalformedOrRetiredPick) {
  auto pick = std::make_unique<PersonalPick>();
  pick->state = PersonalState::Ready;
  pick->count = 9;
  pick->record.book.key.provenance = capture.provenance;
  std::strcpy(pick->record.book.title.data(), "Northbound");
  EXPECT_FALSE(projectRecommendedWidget(*pick, capture, {0, LibraryBytes}, out));
  pick->record.book.title.fill('x');
  EXPECT_FALSE(projectRecommendedWidget(*pick, capture, {1, LibraryBytes}, out));
  EXPECT_EQ(out.count, 0);
  pick->record.book.title[95] = 0;
  pick->record.book.key.provenance.owner = 7;
  pick->record.book.key.provenance.accountEpoch = 2;
  capture.scope = {7, 1};
  EXPECT_TRUE(projectRecommendedWidget(*pick, capture, {1, LibraryBytes}, out));
  EXPECT_EQ(out.state, PersonalWidgetState::NotSaved);
}

TEST_F(Fixture, PublishedBooksRetireWhenCaptureChangesAfterCompletion) {
  ASSERT_TRUE(discover.begin(200, 0, capture, {4, LibraryBytes}));
  run();
  ASSERT_EQ(out.count, 4);
  ++capture.provenance.capture;
  ASSERT_TRUE(discover.tick(capture));
  EXPECT_EQ(out.count, 0);
  for (const auto& book : out.books) EXPECT_EQ(book.title[0], 0);
}
TEST_F(Fixture, DuplicateMaterializedKeysRejectWholePreview) {
  source.duplicateKeys = true;
  ASSERT_TRUE(discover.begin(200, 0, capture, {4, LibraryBytes}));
  bool okay = true;
  while (discover.busy() && okay) okay = discover.tick(capture);
  EXPECT_FALSE(okay);
  EXPECT_EQ(out.state, PersonalWidgetState::Error);
  EXPECT_EQ(out.count, 0);
  for (const auto& book : out.books) EXPECT_EQ(book.title[0], 0);
}
TEST_F(Fixture, MalformedTitleRejectsWholePreview) {
  source.invalidTitle = true;
  ASSERT_TRUE(discover.begin(200, 0, capture, {4, LibraryBytes}));
  bool okay = true;
  while (discover.busy() && okay) okay = discover.tick(capture);
  EXPECT_FALSE(okay);
  EXPECT_EQ(out.state, PersonalWidgetState::Error);
  EXPECT_EQ(out.count, 0);
}
TEST_F(Fixture, PoolCountChangeBetweenSelectionsNeverPublishesPartialBooks) {
  ASSERT_TRUE(discover.begin(200, 0, capture, {4, LibraryBytes}));
  while (!out.books[0].title[0] && discover.busy()) ASSERT_TRUE(discover.tick(capture, 1));
  source.ids.pop_back();
  bool okay = true;
  while (discover.busy() && okay) okay = discover.tick(capture);
  EXPECT_FALSE(okay);
  EXPECT_EQ(out.state, PersonalWidgetState::Error);
  EXPECT_EQ(out.count, 0);
}
TEST_F(Fixture, LargestAcceptedShuffleCursorWrapsWithoutClampDuplicates) {
  ASSERT_TRUE(discover.begin(200, 1000000000, capture, {4, LibraryBytes}));
  run();
  EXPECT_EQ(out.count, 4);
  for (unsigned i = 0; i < 4; ++i)
    for (unsigned j = 0; j < i; ++j) EXPECT_FALSE(out.books[i].key == out.books[j].key);
}
TEST_F(Fixture, FailedProjectionLeavesOtherWidgetSnapshotUntouched) {
  PersonalWidgetValues sibling{};
  auto pick = std::make_unique<PersonalPick>();
  pick->state = PersonalState::Ready;
  pick->count = 42;
  std::strcpy(pick->record.book.title.data(), "Saved recommendation");
  ASSERT_TRUE(projectRecommendedWidget(*pick, capture, {1, LibraryBytes}, sibling));
  EXPECT_FALSE(discover.begin(200, 0, capture, {1, LibraryBytes}));
  EXPECT_EQ(sibling.total, 42);
  EXPECT_EQ(sibling.state, PersonalWidgetState::Ready);
  EXPECT_STREQ(sibling.books[0].title.data(), "Saved recommendation");
}
