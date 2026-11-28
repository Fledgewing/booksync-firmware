#include <gtest/gtest.h>

#include "booksync/services/widgets/Collections.h"
using namespace booksync;
namespace {
LibraryWidgetInput libraryInput() {
  LibraryWidgetInput input;
  input.provenance = {4, 3, 17, 0};
  input.state = PersonalState::Ready;
  input.books = {2418, true};
  input.onCard = {312, true};
  input.authors = {410, true};
  input.series = {92, true};
  input.collections = {9, true};
  input.storageKnown = true;
  input.freeBytes = 4200000000;
  input.totalBytes = 32000000000;
  return input;
}
struct ListsFixture {
  ListsWidgetMeta metadata{{4, 3, 17, 0}, 9, 9, 0, 0, PersonalState::Ready, true, true, false, false};
  unsigned peeks = 0, reads = 0;
  int failedRank = -1;
  bool changed = false, badName = false, wrongOwner = false, duplicate = false;
  ListsWidgetSource source() { return {this, peek, read}; }
  static ListsWidgetMeta peek(void* context) {
    auto& f = *static_cast<ListsFixture*>(context);
    auto meta = f.metadata;
    if (++f.peeks > 1 && f.changed) ++meta.provenance.capture;
    return meta;
  }
  static bool read(void* context, uint32_t rank, ListWidgetEntry& entry) {
    auto& f = *static_cast<ListsFixture*>(context);
    ++f.reads;
    if (int(rank) == f.failedRank) return false;
    entry = {};
    entry.key = {f.metadata.provenance, f.duplicate ? 0 : rank};
    std::snprintf(entry.name.data(), entry.name.size(), "Synthetic list %u", rank);
    entry.books = 84 + rank;
    entry.booksKnown = true;
    if (f.badName) entry.name.fill('X');
    if (f.wrongOwner) ++entry.key.provenance.owner;
    return true;
  }
};
}  // namespace
class LibraryWidgetStates : public testing::TestWithParam<PersonalState> {};
TEST_P(LibraryWidgetStates, IndependentAvailabilityDoesNotInventCounts) {
  auto input = libraryInput();
  input.state = GetParam();
  if (GetParam() == PersonalState::Empty) {
    input.books = {0, true};
    input.onCard = {0, true};
  }
  LibraryWidgetValues out;
  projectLibraryWidget(input, {17, 3}, {LibraryBytes, 2}, out);
  if (GetParam() == PersonalState::Ready)
    EXPECT_EQ(out.state, CollectionWidgetState::Ready);
  else if (GetParam() == PersonalState::Empty) {
    EXPECT_EQ(out.state, CollectionWidgetState::Empty);
    EXPECT_TRUE(out.booksKnown);
  } else {
    EXPECT_FALSE(out.booksKnown);
    EXPECT_FALSE(out.storageKnown);
    EXPECT_EQ(out.rows, 0);
  }
  const CollectionWidgetState states[] = {CollectionWidgetState::NotSaved, CollectionWidgetState::Ready,
                                          CollectionWidgetState::Empty,    CollectionWidgetState::Unsupported,
                                          CollectionWidgetState::Error,    CollectionWidgetState::Off,
                                          CollectionWidgetState::Oversize};
  EXPECT_EQ(out.state, states[static_cast<unsigned>(GetParam())]);
}
INSTANTIATE_TEST_SUITE_P(IndependentStates, LibraryWidgetStates,
                         testing::Values(PersonalState::Ready, PersonalState::Empty, PersonalState::NotSaved,
                                         PersonalState::Off, PersonalState::Unsupported, PersonalState::Unavailable,
                                         PersonalState::Oversized));
TEST(LibraryWidget, KnownZeroDiffersFromMissingAndNoNativeCountSubstitutesForLibrary) {
  auto input = libraryInput();
  input.books = {};
  LibraryWidgetValues out;
  ASSERT_TRUE(projectLibraryWidget(input, {17, 3}, {LibraryBytes, 2}, out));
  EXPECT_FALSE(out.booksKnown);
  EXPECT_EQ(out.onCard, 312);
  input.books = {0, true};
  input.onCard = {0, true};
  ASSERT_TRUE(projectLibraryWidget(input, {17, 3}, {LibraryBytes, 2}, out));
  EXPECT_EQ(out.state, CollectionWidgetState::Empty);
  EXPECT_TRUE(out.booksKnown);
  EXPECT_EQ(out.books, 0);
  input.books = input.onCard = input.authors = input.series = input.collections = {};
  input.storageKnown = false;
  ASSERT_TRUE(projectLibraryWidget(input, {17, 3}, {LibraryBytes, 2}, out));
  EXPECT_EQ(out.state, CollectionWidgetState::NotSaved);
}
TEST(LibraryWidget, LibraryAndNativeCountsStayIndependentAcrossCoverage) {
  auto input = libraryInput();
  input.books = {0, true};
  input.onCard = {312, true};
  input.state = PersonalState::Empty;
  LibraryWidgetValues out;
  ASSERT_TRUE(projectLibraryWidget(input, {17, 3}, {LibraryBytes, 2}, out));
  EXPECT_EQ(out.state, CollectionWidgetState::Ready);
  EXPECT_EQ(out.books, 0);
  EXPECT_EQ(out.onCard, 312);
  EXPECT_EQ(out.rows, 2);
}
TEST(LibraryWidget, TwoRowsAndActualBytesAreRequiredBeforePublishing) {
  auto input = libraryInput();
  LibraryWidgetValues out;
  EXPECT_FALSE(projectLibraryWidget(input, {17, 3}, {LibraryBytes, 1}, out));
  EXPECT_EQ(out.state, CollectionWidgetState::Oversize);
  EXPECT_FALSE(projectLibraryWidget(input, {17, 3}, {sizeof(out) - 1, 2}, out));
  input.bytes = LibraryBytes + 1;
  EXPECT_FALSE(projectLibraryWidget(input, {17, 3}, {LibraryBytes, 2}, out));
  input.bytes = 1024;
  ASSERT_TRUE(projectLibraryWidget(input, {17, 3}, {1024, 2}, out));
  EXPECT_EQ(out.bytes, 1024);
  EXPECT_EQ(out.rows, 2);
}
TEST(LibraryWidget, RetiredAccountDoesNotLeakCountsButNativeOwnerIsIndependent) {
  auto input = libraryInput();
  LibraryWidgetValues out;
  ASSERT_TRUE(projectLibraryWidget(input, {17, 4}, {LibraryBytes, 2}, out));
  EXPECT_EQ(out.state, CollectionWidgetState::NotSaved);
  EXPECT_FALSE(out.booksKnown);
  EXPECT_EQ(out.provenance.owner, 0);
  input.provenance.owner = 0;
  ASSERT_TRUE(projectLibraryWidget(input, {18, 99}, {LibraryBytes, 2}, out));
  EXPECT_EQ(out.books, 2418);
}
TEST(LibraryWidget, InvalidStorageAndCountOverflowRejectOnlyThisCard) {
  auto input = libraryInput();
  LibraryWidgetValues sibling, out;
  ASSERT_TRUE(projectLibraryWidget(input, {17, 3}, {LibraryBytes, 2}, sibling));
  input.freeBytes = input.totalBytes + 1;
  EXPECT_FALSE(projectLibraryWidget(input, {17, 3}, {LibraryBytes, 2}, out));
  EXPECT_EQ(out.state, CollectionWidgetState::Error);
  EXPECT_EQ(sibling.books, 2418);
  input = libraryInput();
  input.authors = {UINT64_MAX, true};
  EXPECT_FALSE(projectLibraryWidget(input, {17, 3}, {LibraryBytes, 2}, out));
  EXPECT_EQ(out.state, CollectionWidgetState::Oversize);
  EXPECT_FALSE(out.booksKnown);
  EXPECT_EQ(sibling.onCard, 312);
}
TEST(LibraryWidget, StaleKeepsActualValuesAndUnknownAgeNeverBecomesZeroAge) {
  auto input = libraryInput();
  input.stale = true;
  input.ageDays = 2;
  input.ageKnown = true;
  LibraryWidgetValues out;
  ASSERT_TRUE(projectLibraryWidget(input, {17, 3}, {LibraryBytes, 2}, out));
  EXPECT_TRUE(out.stale);
  EXPECT_EQ(out.ageDays, 2);
  input.ageKnown = false;
  ASSERT_TRUE(projectLibraryWidget(input, {17, 3}, {LibraryBytes, 2}, out));
  EXPECT_TRUE(out.stale);
  EXPECT_FALSE(out.ageKnown);
  EXPECT_EQ(out.books, 2418);
  EXPECT_EQ(out.ageDays, 0);
}
class ListsWidgetStates : public testing::TestWithParam<PersonalState> {};
TEST_P(ListsWidgetStates, IndependentStatesKeepAbsenceDistinctFromKnownEmpty) {
  ListsFixture fixture;
  fixture.metadata.state = GetParam();
  if (GetParam() == PersonalState::Empty) fixture.metadata.saved = fixture.metadata.total = 0;
  ListsWidgetValues out;
  projectListsWidget(fixture.source(), {17, 3}, {LibraryBytes, 3}, out);
  const CollectionWidgetState states[] = {CollectionWidgetState::NotSaved, CollectionWidgetState::Ready,
                                          CollectionWidgetState::Empty,    CollectionWidgetState::Unsupported,
                                          CollectionWidgetState::Error,    CollectionWidgetState::Off,
                                          CollectionWidgetState::Oversize};
  EXPECT_EQ(out.state, states[static_cast<unsigned>(GetParam())]);
  EXPECT_EQ(out.count, GetParam() == PersonalState::Ready ? 3 : 0);
  EXPECT_EQ(fixture.reads, GetParam() == PersonalState::Ready ? 3 : 0);
}
INSTANTIATE_TEST_SUITE_P(IndependentStates, ListsWidgetStates,
                         testing::Values(PersonalState::Ready, PersonalState::Empty, PersonalState::NotSaved,
                                         PersonalState::Off, PersonalState::Unsupported, PersonalState::Unavailable,
                                         PersonalState::Oversized));
class ListsWidgetSizes : public testing::TestWithParam<uint16_t> {};
TEST_P(ListsWidgetSizes, MainAndOptionalSizeReadOnlyTheirRequestedRows) {
  ListsFixture fixture;
  ListsWidgetValues out;
  ASSERT_TRUE(projectListsWidget(fixture.source(), {17, 3}, {LibraryBytes, GetParam()}, out));
  EXPECT_EQ(out.count, GetParam());
  EXPECT_EQ(fixture.reads, GetParam());
  EXPECT_EQ(out.total, 9);
  EXPECT_TRUE(out.countKnown);
  EXPECT_EQ(fixture.peeks, 2);
  EXPECT_EQ(out.entries[0].books, 84);
  EXPECT_EQ(out.entries[0].key.ordinal, 0);
}
INSTANTIATE_TEST_SUITE_P(ApprovedSizes, ListsWidgetSizes, testing::Values(uint16_t(1), uint16_t(3)));
TEST(ListsWidget, UnknownCountAndPartialMembershipNeverInventZeroOrFullCount) {
  ListsFixture fixture;
  ListsWidgetValues out;
  fixture.metadata.saved = 0;
  fixture.metadata.complete = false;
  ASSERT_TRUE(projectListsWidget(fixture.source(), {17, 3}, {LibraryBytes, 3}, out));
  EXPECT_EQ(out.state, CollectionWidgetState::NotSaved);
  fixture.metadata.saved = 2;
  fixture.metadata.countKnown = false;
  fixture.metadata.total = 0;
  ASSERT_TRUE(projectListsWidget(fixture.source(), {17, 3}, {LibraryBytes, 3}, out));
  EXPECT_EQ(out.count, 2);
  EXPECT_FALSE(out.countKnown);
  EXPECT_EQ(out.total, 0);
  EXPECT_EQ(out.state, CollectionWidgetState::Ready);
}
TEST(ListsWidget, ReadFailureRetiresPartialOutputAndLeavesSiblingUsable) {
  ListsFixture fixture;
  ListsWidgetValues sibling, out;
  ASSERT_TRUE(projectListsWidget(fixture.source(), {17, 3}, {LibraryBytes, 3}, sibling));
  fixture.failedRank = 1;
  EXPECT_FALSE(projectListsWidget(fixture.source(), {17, 3}, {LibraryBytes, 3}, out));
  EXPECT_EQ(out.state, CollectionWidgetState::Error);
  EXPECT_EQ(out.count, 0);
  EXPECT_EQ(out.entries[0].name[0], 0);
  EXPECT_EQ(sibling.count, 3);
}
TEST(ListsWidget, MalformedNamesWrongScopeAndDuplicateIdentitiesFailClosed) {
  for (unsigned kind = 0; kind < 3; ++kind) {
    ListsFixture fixture;
    fixture.badName = kind == 0;
    fixture.wrongOwner = kind == 1;
    fixture.duplicate = kind == 2;
    ListsWidgetValues out;
    EXPECT_FALSE(projectListsWidget(fixture.source(), {17, 3}, {LibraryBytes, 3}, out));
    EXPECT_EQ(out.count, 0);
    EXPECT_EQ(out.entries[0].name[0], 0);
    EXPECT_EQ(out.provenance.owner, 0);
  }
}
TEST(ListsWidget, SourceRetirementAndAccountChangesCannotPublishOldNamesOrCounts) {
  ListsFixture fixture;
  ListsWidgetValues out;
  ASSERT_TRUE(projectListsWidget(fixture.source(), {17, 4}, {LibraryBytes, 3}, out));
  EXPECT_EQ(fixture.reads, 0);
  EXPECT_EQ(out.total, 0);
  fixture.peeks = 0;
  fixture.changed = true;
  ASSERT_TRUE(projectListsWidget(fixture.source(), {17, 3}, {LibraryBytes, 3}, out));
  EXPECT_EQ(out.state, CollectionWidgetState::NotSaved);
  EXPECT_EQ(out.entries[0].name[0], 0);
  EXPECT_EQ(out.count, 0);
  EXPECT_EQ(out.total, 0);
}
TEST(ListsWidget, OversizeRejectedBeforeRecordReadsAndNotHiddenInsideOneRow) {
  ListsFixture fixture;
  ListsWidgetValues out;
  fixture.metadata.bytes = LibraryBytes + 1;
  EXPECT_FALSE(projectListsWidget(fixture.source(), {17, 3}, {LibraryBytes, 3}, out));
  EXPECT_EQ(fixture.reads, 0);
  EXPECT_EQ(out.state, CollectionWidgetState::Oversize);
  fixture.metadata.bytes = 0;
  EXPECT_FALSE(projectListsWidget(fixture.source(), {17, 3}, {1, 3}, out));
  EXPECT_EQ(fixture.reads, 0);
  EXPECT_FALSE(projectListsWidget(fixture.source(), {17, 3}, {LibraryBytes, 0}, out));
  EXPECT_EQ(fixture.reads, 0);
}
TEST(ListsWidget, StaleAndNativeSnapshotsPreserveTheirOwnCountsAndIdentity) {
  ListsFixture fixture;
  ListsWidgetValues out;
  fixture.metadata.provenance.owner = 0;
  fixture.metadata.stale = true;
  fixture.metadata.ageKnown = true;
  fixture.metadata.ageDays = 2;
  ASSERT_TRUE(projectListsWidget(fixture.source(), {99, 99}, {LibraryBytes, 3}, out));
  EXPECT_EQ(out.total, 9);
  EXPECT_EQ(out.count, 3);
  EXPECT_TRUE(out.stale);
  EXPECT_TRUE(out.ageKnown);
  EXPECT_EQ(out.ageDays, 2);
  EXPECT_EQ(out.entries[0].key.provenance.owner, 0);
}

TEST(CollectionWidgets, APartialOptionalAllowanceCannotConcealItsMissingThirdRow) {
  ListsWidgetValues out;
  EXPECT_FALSE(projectListsWidget({}, {}, {LibraryBytes, 2}, out));
  EXPECT_EQ(out.state, CollectionWidgetState::Oversize);
  EXPECT_EQ(out.count, 0);
}

TEST(SavedListsPage, FullHeaderCountAndActualShortPagesRemainSeparate) {
  ListsFixture fixture;
  SavedListsPage out;
  ASSERT_TRUE(projectSavedListsPage(fixture.source(), {17, 3}, {0, LibraryBytes, 4}, out));
  EXPECT_EQ(out.total, 9);
  EXPECT_EQ(out.count, 4);
  EXPECT_EQ(fixture.reads, 4);
  EXPECT_TRUE(out.hasMore);
  fixture.reads = fixture.peeks = 0;
  ASSERT_TRUE(projectSavedListsPage(fixture.source(), {17, 3}, {8, LibraryBytes, 4}, out));
  EXPECT_EQ(out.count, 1);
  EXPECT_EQ(out.entries[0].key.ordinal, 8);
  EXPECT_FALSE(out.hasMore);
  EXPECT_EQ(fixture.reads, 1);
}
TEST(SavedListsPage, RetirementBudgetAndMalformedNamesNeverPublishPartialMembership) {
  ListsFixture fixture;
  SavedListsPage out;
  EXPECT_FALSE(projectSavedListsPage(fixture.source(), {17, 3}, {0, 1, 12}, out));
  EXPECT_EQ(fixture.reads, 0);
  EXPECT_EQ(fixture.peeks, 0);
  ASSERT_TRUE(projectSavedListsPage(fixture.source(), {17, 4}, {0, LibraryBytes, 12}, out));
  EXPECT_EQ(out.state, CollectionWidgetState::NotSaved);
  EXPECT_EQ(fixture.reads, 0);
  fixture.peeks = 0;
  fixture.changed = true;
  ASSERT_TRUE(projectSavedListsPage(fixture.source(), {17, 3}, {0, LibraryBytes, 12}, out));
  EXPECT_EQ(out.count, 0);
  EXPECT_EQ(out.entries[0].name[0], 0);
  fixture.changed = false;
  fixture.badName = true;
  fixture.peeks = 0;
  EXPECT_FALSE(projectSavedListsPage(fixture.source(), {17, 3}, {0, LibraryBytes, 12}, out));
  EXPECT_EQ(out.state, CollectionWidgetState::Error);
  EXPECT_EQ(out.count, 0);
}
TEST(LibraryWidget, StorageBarIsPreparedOutsideTheRenderer) {
  auto input = libraryInput();
  LibraryWidgetValues out;
  ASSERT_TRUE(projectLibraryWidget(input, {17, 3}, {LibraryBytes, 2}, out));
  EXPECT_EQ(out.storageUsedPercent, 87);
}
