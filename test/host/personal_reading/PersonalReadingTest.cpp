#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "booksync/services/PersonalReading.h"
using namespace booksync;
namespace {
class Provider : public PersonalReadingSource {
 public:
  std::vector<std::string> ids;
  ReadingScope scope{};
  PersonalState state = PersonalState::Ready;
  unsigned reads = 0;
  PersonalCard fields{};
  PersonalState availability(PersonalKind) const override { return state; }
  ReadingScope owner() const override { return scope; }
  PersonalStep read(PersonalKind, uint32_t n, PersonalRecord& out) override {
    ++reads;
    if (n >= ids.size()) return PersonalStep::End;
    out = {};
    std::strcpy(out.identity.data(), ids[n].c_str());
    out.book.page = n;
    return PersonalStep::Record;
  }
  PersonalStep find(PersonalKind, std::string_view id, uint32_t& ordinal) override {
    for (size_t i = 0; i < ids.size(); ++i)
      if (ids[i] == id) {
        ordinal = i;
        return PersonalStep::Record;
      }
    return PersonalStep::End;
  }
  bool card(BookKey, PersonalCard& out) override {
    out = fields;
    return true;
  }
};
void run(PersonalReading& personal) {
  for (unsigned i = 0; personal.busy() && i < 100000; ++i) ASSERT_TRUE(personal.tick(7));
  ASSERT_FALSE(personal.busy());
}
}  // namespace
TEST(PersonalReading, RegistrationWinsDuplicateAndHighlightUsesFloorModulo) {
  Provider a, b;
  a.ids = {"a", "b"};
  b.ids = {"b", "c"};
  PersonalReadingSource* sources[] = {&a, &b};
  PersonalReading p(sources, 2);
  ASSERT_TRUE(p.begin(PersonalKind::Highlight, -1, 0, {7, 1}));
  run(p);
  EXPECT_STREQ(p.result().record.identity.data(), "c");
  EXPECT_EQ(p.result().count, 3);
  ASSERT_TRUE(p.begin(PersonalKind::Recommended, 0, 0, {7, 1}));
  run(p);
  EXPECT_STREQ(p.result().record.identity.data(), "a");
}
TEST(PersonalReading, DiscoverHasStableCycleAcrossRestartAndSourceOrder) {
  Provider a;
  a.ids = {"alpha", "beta", "gamma", "delta"};
  PersonalReadingSource* sources[] = {&a};
  std::vector<std::string> selected;
  for (int i = 0; i < 5; ++i) {
    PersonalReading p(sources, 1);
    ASSERT_TRUE(p.begin(PersonalKind::Discover, 200, i, {7, 1}));
    run(p);
    selected.emplace_back(p.result().record.identity.data());
  }
  EXPECT_EQ(selected[0], selected[4]);
  std::sort(selected.begin(), selected.begin() + 4);
  EXPECT_EQ(std::unique(selected.begin(), selected.begin() + 4), selected.begin() + 4);
}
TEST(PersonalReading, ProviderFailureEmptyUnsupportedAndAccountIsolation) {
  Provider a;
  a.scope = {8, 1};
  a.ids = {"private"};
  PersonalReadingSource* sources[] = {&a};
  PersonalReading p(sources, 1);
  ASSERT_TRUE(p.begin(PersonalKind::Highlight, 200, 0, {7, 1}));
  run(p);
  EXPECT_NE(p.result().state, PersonalState::Ready);
  a.scope = {};
  a.ids.clear();
  ASSERT_TRUE(p.begin(PersonalKind::Highlight, 201, 0, {7, 1}));
  run(p);
  EXPECT_EQ(p.result().state, PersonalState::Empty);
  a.state = PersonalState::Unavailable;
  ASSERT_TRUE(p.begin(PersonalKind::Highlight, 202, 0, {7, 1}));
  run(p);
  EXPECT_EQ(p.result().state, PersonalState::Unavailable);
}
TEST(PersonalReading, CardFillsAbsentFieldsButPreservesKnownZero) {
  Provider a, b;
  a.fields.progress = {0, true};
  b.fields.progress = {50, true};
  b.fields.timeLeft = {10, true};
  PersonalReadingSource* sources[] = {&a, &b};
  PersonalReading p(sources, 2);
  PersonalCard out;
  ASSERT_TRUE(p.card({}, {7, 1}, out));
  EXPECT_EQ(out.progress.value, 0);
  EXPECT_EQ(out.timeLeft.value, 10);
}
TEST(PersonalReading, SourcePriorityAndRepeatedIdentitiesNeverChangePoolSize) {
  Provider a, b;
  a.ids = {"a", "a", "b"};
  b.ids = {"a", "b", "c"};
  PersonalReadingSource* sources[] = {&a, &b};
  PersonalReading p(sources, 2);
  ASSERT_TRUE(p.begin(PersonalKind::Highlight, 0, 0, {7, 1}));
  run(p);
  EXPECT_EQ(p.result().count, 3);
  EXPECT_EQ(p.result().record.book.page, 0);
}
TEST(PersonalReading, DiscoverMatchesCompanionSha256Ordering) {
  // Python hashlib.sha256(b"200:" + identity).hexdigest() ascending.
  Provider a;
  a.ids = {"alpha", "beta", "gamma", "delta"};
  PersonalReadingSource* sources[] = {&a};
  PersonalReading p(sources, 1);
  const char* expected[] = {"beta", "delta", "alpha", "gamma"};
  for (unsigned i = 0; i < 4; ++i) {
    ASSERT_TRUE(p.begin(PersonalKind::Discover, 200, i, {7, 1}));
    run(p);
    EXPECT_STREQ(p.result().record.identity.data(), expected[i]);
  }
}
TEST(PersonalReading, UnsupportedOffAndNotSavedRemainDistinct) {
  Provider a;
  PersonalReadingSource* sources[] = {&a};
  PersonalReading p(sources, 1);
  for (auto state : {PersonalState::Unsupported, PersonalState::Off, PersonalState::NotSaved}) {
    a.state = state;
    ASSERT_TRUE(p.begin(PersonalKind::YearAgo, 200, 0, {7, 1}));
    run(p);
    EXPECT_EQ(p.result().state, state);
  }
}
TEST(PersonalReading, TenThousandSavedRowsUseBoundedTicksAndExactCycle) {
  class Large final : public PersonalReadingSource {
   public:
    uint32_t reads = 0;
    ReadingScope owner() const override { return {}; }
    PersonalState availability(PersonalKind) const override { return PersonalState::Ready; }
    PersonalStep read(PersonalKind, uint32_t n, PersonalRecord& out) override {
      ++reads;
      if (n == 10000) return PersonalStep::End;
      out = {};
      snprintf(out.identity.data(), out.identity.size(), "%u", n);
      return PersonalStep::Record;
    }
    PersonalStep find(PersonalKind, std::string_view id, uint32_t& ordinal) override {
      ordinal = 0;
      for (char c : id) {
        if (c < '0' || c > '9') return PersonalStep::Error;
        ordinal = ordinal * 10 + c - '0';
      }
      return ordinal < 10000 ? PersonalStep::Record : PersonalStep::End;
    }
  } a;
  PersonalReadingSource* sources[] = {&a};
  PersonalReading p(sources, 1);
  ASSERT_TRUE(p.begin(PersonalKind::Discover, 200, 9999, {7, 1}));
  while (p.busy()) {
    const auto before = a.reads;
    ASSERT_TRUE(p.tick());
    EXPECT_LE(a.reads - before, PersonalReading::Batch);
  }
  EXPECT_EQ(p.result().count, 10000);
  EXPECT_EQ(p.result().state, PersonalState::Ready);
  EXPECT_LT(a.reads, 34 * 10001u);
}
TEST(PersonalReading, FailedProviderDiscardsPartialRowsAndFallsBackLikeCompanion) {
  class Broken final : public Provider {
   public:
    PersonalStep read(PersonalKind kind, uint32_t n, PersonalRecord& out) override {
      return n == 1 ? PersonalStep::Error : Provider::read(kind, n, out);
    }
  } broken;
  broken.ids = {"partial", "never"};
  Provider good;
  good.ids = {"saved"};
  PersonalReadingSource* sources[] = {&broken, &good};
  PersonalReading p(sources, 2);
  ASSERT_TRUE(p.begin(PersonalKind::Recommended, 200, 0, {7, 1}));
  run(p);
  EXPECT_EQ(p.result().state, PersonalState::Ready);
  EXPECT_STREQ(p.result().record.identity.data(), "saved");
  EXPECT_EQ(p.result().count, 1);
}

TEST(PersonalReading, CardRejectsRetiredAccountBookBeforeProviderRead) {
  Provider a;
  a.fields.progress = {50, true};
  PersonalReadingSource* sources[] = {&a};
  PersonalReading p(sources, 1);
  PersonalCard out;
  BookKey book;
  book.provenance.owner = 7;
  book.provenance.accountEpoch = 1;
  EXPECT_FALSE(p.card(book, {7, 2}, out));
  EXPECT_FALSE(out.progress.known);
  EXPECT_FALSE(p.card(book, {8, 1}, out));
  EXPECT_FALSE(out.progress.known);
}
