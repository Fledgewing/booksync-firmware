#include <gtest/gtest.h>

#include <cstdio>

#include "booksync/services/widgets/BookList.h"
using namespace booksync;
namespace {
constexpr WindowSource presets[] = {WindowSource::AlsoReading,  WindowSource::Recent,     WindowSource::WantToRead,
                                    WindowSource::AllFinished,  WindowSource::PickBackUp, WindowSource::UpNext,
                                    WindowSource::FinishedYear, WindowSource::Unopened,   WindowSource::OnDevice};
constexpr uint32_t order[][4] = {{90, 12, 44, 3}, {502, 501, 500, 499}, {73, 2, 91, 11},
                                 {41, 4, 17, 6},  {600, 400, 200, 100}, {9, 2, 7, 1},
                                 {61, 14, 36, 5}, {88, 31, 22, 19},     {20, 5, 3, 1}};
struct Fake {
  BookListMeta meta{};
  unsigned reads = 0, peeks = 0;
  int failAt = -1;
  bool change = false, badTitle = false, badOwner = false, duplicate = false;
  WindowSource seen = WindowSource::Unknown;
  Fake() {
    meta.state = PersonalState::Ready;
    meta.total = meta.saved = 10000;
    meta.countKnown = meta.complete = true;
    meta.year = 2026;
    meta.provenance = {4, 3, 17, 0};
  }
  BookListSource source() { return {this, peek, read}; }
  static BookListMeta peek(void* context, WindowSource source) {
    auto& f = *static_cast<Fake*>(context);
    f.seen = source;
    auto out = f.meta;
    if (++f.peeks > 1 && f.change) ++out.provenance.accountEpoch;
    return out;
  }
  static bool read(void* context, WindowSource source, uint32_t index, BookRow& out) {
    auto& f = *static_cast<Fake*>(context);
    ++f.reads;
    if (int(index) == f.failAt) return false;
    unsigned preset = 0;
    while (preset < 9 && presets[preset] != source) ++preset;
    if (preset == 9 || index >= 4) return false;
    out = {};
    out.key = {f.meta.provenance, order[preset][f.duplicate && index == 1 ? 0 : index]};
    if (f.badOwner) ++out.key.provenance.owner;
    std::snprintf(out.title.data(), out.title.size(), "Synthetic book %u", out.key.ordinal);
    if (f.badTitle) out.title.fill('x');
    return true;
  }
};
WindowRequest request(WindowSource source) { return {0, 4, LibraryBytes, source}; }
ReadingScope account{17, 3};
}  // namespace
class BookListPresets : public testing::TestWithParam<unsigned> {};
TEST_P(BookListPresets, KeepsIndependentSourceOrderCountsAndMainRowBudget) {
  Fake f;
  BookListProjection out;
  const unsigned preset = GetParam();
  ASSERT_TRUE(projectBookList(f.source(), request(presets[preset]), account, 2026, out));
  const unsigned count = preset == 0 || preset == 8 ? 3 : 4;
  EXPECT_EQ(f.seen, presets[preset]);
  EXPECT_EQ(out.state, PersonalState::Ready);
  EXPECT_EQ(out.total, 10000u);
  EXPECT_EQ(out.count, count);
  EXPECT_EQ(f.reads, count);
  EXPECT_EQ(f.peeks, 2u);
  EXPECT_LT(out.bytes, LibraryBytes);
  for (unsigned i = 0; i < count; ++i) EXPECT_EQ(out.books[i].key.ordinal, order[preset][i]);
}
INSTANTIATE_TEST_SUITE_P(AllNine, BookListPresets, testing::Range(0u, 9u));
class BookListStates : public testing::TestWithParam<PersonalState> {};
TEST_P(BookListStates, DistinctAvailabilityDoesNoRecordWork) {
  Fake f;
  f.meta.state = GetParam();
  if (GetParam() == PersonalState::Empty) f.meta.saved = f.meta.total = 0;
  BookListProjection out;
  projectBookList(f.source(), request(WindowSource::Recent), account, 2026, out);
  EXPECT_EQ(out.state, GetParam());
  EXPECT_EQ(out.count, 0u);
  EXPECT_EQ(f.reads, 0u);
}
INSTANTIATE_TEST_SUITE_P(IndependentStates, BookListStates,
                         testing::Values(PersonalState::Empty, PersonalState::NotSaved, PersonalState::Off,
                                         PersonalState::Unsupported, PersonalState::Unavailable,
                                         PersonalState::Oversized));
TEST(BookListProjection, CompleteZeroAndUnknownOrIncompleteEmptyDiffer) {
  Fake f;
  f.meta.saved = f.meta.total = 0;
  BookListProjection out;
  ASSERT_TRUE(projectBookList(f.source(), request(WindowSource::Recent), account, 2026, out));
  EXPECT_EQ(out.state, PersonalState::Empty);
  f.meta.countKnown = false;
  projectBookList(f.source(), request(WindowSource::Recent), account, 2026, out);
  EXPECT_EQ(out.state, PersonalState::NotSaved);
  f.meta.countKnown = true;
  f.meta.complete = false;
  projectBookList(f.source(), request(WindowSource::Recent), account, 2026, out);
  EXPECT_EQ(out.state, PersonalState::NotSaved);
  EXPECT_EQ(f.reads, 0u);
}
TEST(BookListProjection, YearNeverComesFromOpeningHistoryOrAnOldSnapshot) {
  Fake f;
  BookListProjection out;
  projectBookList(f.source(), request(WindowSource::FinishedYear), account, 0, out);
  EXPECT_EQ(out.state, PersonalState::NotSaved);
  projectBookList(f.source(), request(WindowSource::FinishedYear), account, 2027, out);
  EXPECT_EQ(out.state, PersonalState::NotSaved);
  EXPECT_EQ(f.reads, 0u);
  ASSERT_TRUE(projectBookList(f.source(), request(WindowSource::AllFinished), account, 0, out));
  EXPECT_EQ(out.state, PersonalState::Ready);
}
TEST(BookListProjection, StaleKeepsActualRowsAndAuthoritativeTotal) {
  Fake f;
  f.meta.stale = true;
  f.meta.saved = 2;
  f.meta.total = 41;
  f.meta.complete = false;
  BookListProjection out;
  ASSERT_TRUE(projectBookList(f.source(), request(WindowSource::WantToRead), account, 2026, out));
  EXPECT_TRUE(out.stale);
  EXPECT_FALSE(out.complete);
  EXPECT_EQ(out.count, 2u);
  EXPECT_EQ(out.total, 41u);
  EXPECT_EQ(f.reads, 2u);
}
TEST(BookListProjection, OversizeAndShortRowAllowanceRejectBeforeReading) {
  Fake f;
  f.meta.bytes = LibraryBytes + 1;
  BookListProjection out;
  EXPECT_FALSE(projectBookList(f.source(), request(WindowSource::Recent), account, 2026, out));
  EXPECT_EQ(out.state, PersonalState::Oversized);
  EXPECT_EQ(f.reads, 0u);
  f.meta.bytes = 0;
  auto shortRequest = request(WindowSource::Recent);
  shortRequest.limit = 3;
  EXPECT_FALSE(projectBookList(f.source(), shortRequest, account, 2026, out));
  EXPECT_EQ(out.state, PersonalState::Oversized);
  EXPECT_EQ(f.reads, 0u);
}
TEST(BookListProjection, FailureDiscardsPartialRowsWithoutMutatingSibling) {
  Fake f;
  BookListProjection sibling, failed;
  ASSERT_TRUE(projectBookList(f.source(), request(WindowSource::OnDevice), account, 2026, sibling));
  f.failAt = 2;
  EXPECT_FALSE(projectBookList(f.source(), request(WindowSource::Recent), account, 2026, failed));
  EXPECT_EQ(failed.state, PersonalState::Unavailable);
  EXPECT_TRUE(failed.request == request(WindowSource::Recent));
  EXPECT_EQ(failed.count, 0u);
  EXPECT_EQ(failed.total, 0u);
  EXPECT_EQ(failed.books[0].title[0], 0);
  EXPECT_EQ(sibling.count, 3u);
  EXPECT_EQ(sibling.books[0].key.ordinal, 20u);
}
TEST(BookListProjection, AccountOrEpochChangeCannotPublishForeignRowsOrCounts) {
  Fake f;
  BookListProjection out;
  projectBookList(f.source(), request(WindowSource::Recent), {18, 3}, 2026, out);
  EXPECT_EQ(out.state, PersonalState::NotSaved);
  EXPECT_EQ(out.total, 0u);
  EXPECT_EQ(f.reads, 0u);
  f.change = true;
  f.peeks = 0;
  projectBookList(f.source(), request(WindowSource::Recent), account, 2026, out);
  EXPECT_EQ(out.state, PersonalState::NotSaved);
  EXPECT_EQ(out.count, 0u);
  EXPECT_EQ(out.books[0].title[0], 0);
}
TEST(BookListProjection, InvalidStringsOrMixedScopeAreNotPaintable) {
  Fake f;
  BookListProjection out;
  f.badTitle = true;
  EXPECT_FALSE(projectBookList(f.source(), request(WindowSource::Recent), account, 2026, out));
  EXPECT_EQ(out.state, PersonalState::Unavailable);
  f.badTitle = false;
  f.badOwner = true;
  EXPECT_FALSE(projectBookList(f.source(), request(WindowSource::Recent), account, 2026, out));
  EXPECT_EQ(out.count, 0u);
}

TEST(BookListProjection, ExplicitEmptyStillRequiresCompleteCountEvidence) {
  Fake f;
  f.meta.state = PersonalState::Empty;
  f.meta.saved = f.meta.total = 0;
  f.meta.countKnown = false;
  BookListProjection out;
  ASSERT_TRUE(projectBookList(f.source(), request(WindowSource::Recent), account, 2026, out));
  EXPECT_EQ(out.state, PersonalState::NotSaved);
  EXPECT_EQ(f.reads, 0u);
}
TEST(BookListProjection, MetadataItselfIsInsideTheCallerByteAllowance) {
  Fake f;
  BookListProjection out;
  auto small = request(WindowSource::Recent);
  small.bytes = 0;
  EXPECT_FALSE(projectBookList(f.source(), small, account, 2026, out));
  EXPECT_EQ(out.state, PersonalState::Oversized);
  EXPECT_EQ(f.peeks, 0u);
  EXPECT_EQ(f.reads, 0u);
}
TEST(BookListProjection, NativeSelectionSurvivesUnrelatedAccountEpochs) {
  Fake f;
  f.meta.provenance.owner = 0;
  BookListProjection out;
  ASSERT_TRUE(projectBookList(f.source(), request(WindowSource::OnDevice), {90, 400}, 0, out));
  EXPECT_EQ(out.state, PersonalState::Ready);
  EXPECT_EQ(out.count, 3u);
  EXPECT_EQ(out.provenance.owner, 0u);
}

TEST(BookListProjection, DuplicateBookKeysNeverBecomeTwoVisibleRecords) {
  Fake f;
  f.duplicate = true;
  BookListProjection out;
  EXPECT_FALSE(projectBookList(f.source(), request(WindowSource::Recent), account, 2026, out));
  EXPECT_EQ(out.state, PersonalState::Unavailable);
  EXPECT_EQ(out.count, 0u);
  EXPECT_EQ(out.total, 0u);
}

TEST(BookListProjection, FileEvidenceRemainsIndependentAndRetiresWithItsSnapshot) {
  Fake f;
  BookListProjection out;
  auto source = f.source();
  ASSERT_TRUE(projectBookList(source, request(WindowSource::Recent), account, 2026, out));
  for (auto file : out.files) EXPECT_EQ(file, FileAvailability::Unknown);
  source.file = [](void*, WindowSource, uint32_t rank) {
    return rank % 2 ? FileAvailability::ServerOnly : FileAvailability::OnCard;
  };
  ASSERT_TRUE(projectBookList(source, request(WindowSource::Recent), account, 2026, out));
  EXPECT_EQ(out.files[0], FileAvailability::OnCard);
  EXPECT_EQ(out.files[1], FileAvailability::ServerOnly);
  f.change = true;
  f.peeks = 0;
  ASSERT_TRUE(projectBookList(source, request(WindowSource::Recent), account, 2026, out));
  EXPECT_EQ(out.state, PersonalState::NotSaved);
  for (auto file : out.files) EXPECT_EQ(file, FileAvailability::Unknown);
}
