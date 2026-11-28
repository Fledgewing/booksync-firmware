#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "booksync/services/NativePersonalSource.h"
using namespace booksync;
namespace {
struct SourceFixture : testing::Test {
  NativePersonalState current{{7, 1, 0, 9}, 1, true};
  std::vector<std::vector<std::string>> books{{"alpha", "beta"}};
  NativePassageStep forced = NativePassageStep::Record;
  uint32_t calls = 0;
  NativePersonalSource source{{this, state, passage}};
  static NativePersonalState state(void* p) { return static_cast<SourceFixture*>(p)->current; }
  static NativePassageStep passage(void* p, uint16_t book, uint16_t clipping, PersonalRecord& out) {
    auto& self = *static_cast<SourceFixture*>(p);
    ++self.calls;
    if (self.forced != NativePassageStep::Record) return self.forced;
    if (clipping >= self.books[book].size()) return NativePassageStep::End;
    out = {};
    std::snprintf(out.identity.data(), out.identity.size(), "%s", self.books[book][clipping].c_str());
    std::snprintf(out.passage.data(), out.passage.size(), "Saved passage %u", clipping);
    out.book.key.ordinal = book;
    return NativePassageStep::Record;
  }
  void prepare() {
    ASSERT_TRUE(source.begin());
    while (source.busy()) ASSERT_TRUE(source.tick());
  }
};
TEST_F(SourceFixture, AbsentIndexDoesNotClaimEmpty) {
  current.ready = false;
  prepare();
  EXPECT_EQ(source.availability(PersonalKind::Highlight), PersonalState::NotSaved);
  EXPECT_EQ(calls, 0);
}
TEST_F(SourceFixture, CompleteEmptyIndexIsEmpty) {
  current.books = 0;
  prepare();
  EXPECT_EQ(source.availability(PersonalKind::Highlight), PersonalState::Empty);
}
TEST_F(SourceFixture, NoCaptureIsNotSaved) {
  forced = NativePassageStep::Missing;
  prepare();
  EXPECT_EQ(source.availability(PersonalKind::Highlight), PersonalState::NotSaved);
}
TEST_F(SourceFixture, CompleteEmptyCaptureIsEmpty) {
  books[0].clear();
  prepare();
  EXPECT_EQ(source.availability(PersonalKind::Highlight), PersonalState::Empty);
}
TEST_F(SourceFixture, UnknownKindsAreUnsupported) {
  prepare();
  for (auto kind : {PersonalKind::YearAgo, PersonalKind::Recommended, PersonalKind::Discover})
    EXPECT_EQ(source.availability(kind), PersonalState::Unsupported);
}
TEST_F(SourceFixture, ReadsAndFindsExactSavedIdentity) {
  prepare();
  PersonalRecord out;
  uint32_t ordinal = 99;
  EXPECT_EQ(source.find(PersonalKind::Highlight, "alpha", ordinal), PersonalStep::Record);
  ASSERT_EQ(source.read(PersonalKind::Highlight, ordinal, out), PersonalStep::Record);
  EXPECT_STREQ(out.identity.data(), "alpha");
  EXPECT_EQ(source.find(PersonalKind::Highlight, "absent", ordinal), PersonalStep::End);
  EXPECT_EQ(source.read(PersonalKind::Highlight, 2, out), PersonalStep::End);
}
TEST_F(SourceFixture, DuplicateLookupReturnsFirstFrozenOrdinal) {
  books[0] = {"alpha", "alpha", "beta"};
  prepare();
  uint32_t ordinal = 99;
  ASSERT_EQ(source.find(PersonalKind::Highlight, "alpha", ordinal), PersonalStep::Record);
  PersonalRecord out;
  ASSERT_EQ(source.read(PersonalKind::Highlight, ordinal, out), PersonalStep::Record);
  for (uint32_t i = 0; i < ordinal; ++i) {
    ASSERT_EQ(source.read(PersonalKind::Highlight, i, out), PersonalStep::Record);
    EXPECT_STRNE(out.identity.data(), "alpha");
  }
}
TEST_F(SourceFixture, HashCollisionDoesNotMergeDifferentIdentities) {
  // Independently known FNV-1a collision, not generated with production helper.
  books[0] = {"costarring", "liquid"};
  prepare();
  uint32_t a = 0, b = 0;
  ASSERT_EQ(source.find(PersonalKind::Highlight, "costarring", a), PersonalStep::Record);
  ASSERT_EQ(source.find(PersonalKind::Highlight, "liquid", b), PersonalStep::Record);
  EXPECT_NE(a, b);
}
TEST_F(SourceFixture, PreparationYieldsAfterOneCapture) {
  ASSERT_TRUE(source.begin());
  EXPECT_TRUE(source.busy());
  EXPECT_EQ(calls, 0);
  ASSERT_TRUE(source.tick());
  EXPECT_EQ(calls, 1);
  EXPECT_TRUE(source.busy());
  EXPECT_FALSE(source.tick(17));
  EXPECT_EQ(calls, 1);
}
TEST_F(SourceFixture, GenerationChangeDiscardsPartialAndPublishedData) {
  ASSERT_TRUE(source.begin());
  ASSERT_TRUE(source.tick());
  ++current.provenance.generation;
  EXPECT_FALSE(source.tick());
  EXPECT_FALSE(source.busy());
  EXPECT_EQ(source.availability(PersonalKind::Highlight), PersonalState::Unavailable);
  prepare();
  ++current.provenance.capture;
  PersonalRecord out;
  EXPECT_EQ(source.read(PersonalKind::Highlight, 0, out), PersonalStep::Error);
}
TEST_F(SourceFixture, AccountEpochChangeCannotPublishOldNativeBatch) {
  prepare();
  ++current.provenance.accountEpoch;
  EXPECT_EQ(source.availability(PersonalKind::Highlight), PersonalState::Unavailable);
}
TEST_F(SourceFixture, FailedCaptureDiscardsPartialPool) {
  ASSERT_TRUE(source.begin());
  ASSERT_TRUE(source.tick());
  forced = NativePassageStep::Error;
  EXPECT_FALSE(source.tick());
  EXPECT_EQ(source.availability(PersonalKind::Highlight), PersonalState::Unavailable);
}
TEST_F(SourceFixture, OversizeNeverPublishesPartialPool) {
  forced = NativePassageStep::Oversized;
  EXPECT_TRUE(source.begin());
  EXPECT_FALSE(source.tick());
  EXPECT_EQ(source.availability(PersonalKind::Highlight), PersonalState::Oversized);
}
TEST_F(SourceFixture, RetireReleasesPublicationAndAllowsFreshBatch) {
  prepare();
  source.retire();
  EXPECT_EQ(source.availability(PersonalKind::Highlight), PersonalState::NotSaved);
  prepare();
  EXPECT_EQ(source.availability(PersonalKind::Highlight), PersonalState::Ready);
}
TEST_F(SourceFixture, TenThousandRecordsHaveIndexedLookup) {
  books.assign(40, {});
  current.books = 40;
  for (unsigned i = 0; i < 10000; ++i) books[i / 250].push_back("native-record-" + std::to_string(i));
  prepare();
  const auto before = calls;
  uint32_t ordinal = 0;
  ASSERT_EQ(source.find(PersonalKind::Highlight, "native-record-7341", ordinal), PersonalStep::Record);
  EXPECT_LE(calls - before, 2u);
  PersonalRecord out;
  EXPECT_EQ(source.read(PersonalKind::Highlight, 10000, out), PersonalStep::End);
}
TEST_F(SourceFixture, TenThousandAndOneIsExplicitlyOversized) {
  books.assign(40, {});
  current.books = 40;
  for (unsigned i = 0; i < 10001; ++i) books[i / 251].push_back("native-record-" + std::to_string(i));
  ASSERT_TRUE(source.begin());
  bool okay = true;
  while (source.busy() && okay) okay = source.tick();
  EXPECT_FALSE(okay);
  EXPECT_EQ(source.availability(PersonalKind::Highlight), PersonalState::Oversized);
}
}  // namespace
