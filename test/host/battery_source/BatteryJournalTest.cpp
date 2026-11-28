// SPDX-License-Identifier: GPL-3.0-or-later
#include <HalStorage.h>
#include <gtest/gtest.h>

#include "booksync/storage/BatteryJournal.h"
using namespace booksync;
using namespace booksync::storage;
namespace {
battery::Record sample(unsigned n) {
  battery::Record r;
  r.t = 1795600000 + n;
  r.kind = battery::KIND_LEVEL;
  r.pct = 90 - n % 80;
  return r;
}
class BatteryJournalTest : public testing::Test {
  void SetUp() override {
    card = {};
    card.disk["/battery"] = "";
    card.files = card.disk;
  }
};
}  // namespace
TEST_F(BatteryJournalTest, MissingReadNeverCreatesStorage) {
  card = {};
  BatteryJournal r("/battery");
  EXPECT_EQ(r.recover(), JournalResult::NotFound);
  EXPECT_TRUE(card.disk.empty());
}
TEST_F(BatteryJournalTest, SegmentBoundaryAndRestartKeepExactNativeMeasurements) {
  BatteryJournal w("/battery");
  ASSERT_EQ(w.recover(true), JournalResult::Ok);
  for (unsigned i = 0; i < 70; ++i) ASSERT_EQ(w.append(sample(i)), JournalResult::Ok);
  card.restart();
  BatteryJournal r("/battery");
  ASSERT_EQ(r.recover(), JournalResult::Ok);
  ASSERT_EQ(r.count(), 70u);
  for (unsigned i = 0; i < 70; ++i) {
    battery::Record got;
    ASSERT_EQ(r.read(i, got), JournalResult::Ok);
    EXPECT_EQ(got, sample(i));
  }
}
TEST_F(BatteryJournalTest, EveryPublicationCutKeepsACompletePrefixAndIdempotentRetry) {
  BatteryJournal w("/battery");
  ASSERT_EQ(w.recover(true), JournalResult::Ok);
  ASSERT_EQ(w.append(sample(0)), JournalResult::Ok);
  const auto seed = card.disk;
  card.events.clear();
  ASSERT_EQ(w.append(sample(1)), JournalResult::Ok);
  auto operations = card.events.size();
  for (bool eager : {false, true})
    for (size_t cut = 0; cut < operations; ++cut) {
      SCOPED_TRACE(cut);
      card.disk = seed;
      card.restart();
      card.eager = eager;
      BatteryJournal before("/battery");
      ASSERT_EQ(before.recover(), JournalResult::Ok);
      card.events.clear();
      card.stop = cut;
      EXPECT_THROW(before.append(sample(1)), PowerCut);
      card.restart();
      BatteryJournal after("/battery");
      ASSERT_EQ(after.recover(), JournalResult::Ok);
      ASSERT_EQ(after.append(sample(1)), JournalResult::Ok);
      EXPECT_EQ(after.count(), 2u);
      battery::Record first;
      ASSERT_EQ(after.read(0, first), JournalResult::Ok);
      EXPECT_EQ(first, sample(0));
    }
}
TEST_F(BatteryJournalTest, CapacityAddsPermanentGapWithoutEviction) {
  BatteryJournal w("/battery", 2);
  ASSERT_EQ(w.recover(true), JournalResult::Ok);
  ASSERT_EQ(w.append(sample(0)), JournalResult::Ok);
  ASSERT_EQ(w.append(sample(1)), JournalResult::Ok);
  EXPECT_EQ(w.append(sample(2)), JournalResult::Capacity);
  card.restart();
  BatteryJournal r("/battery", 2);
  ASSERT_EQ(r.recover(), JournalResult::Ok);
  EXPECT_TRUE(r.capped());
  EXPECT_EQ(r.count(), 3u);
  battery::Record last;
  ASSERT_EQ(r.read(2, last), JournalResult::Ok);
  EXPECT_EQ(last.kind, battery::KIND_GAP);
  EXPECT_EQ(r.append(sample(3)), JournalResult::Capacity);
  EXPECT_EQ(r.count(), 3u);
}
TEST_F(BatteryJournalTest, FutureAndCorruptCommittedSourcesAreRetained) {
  for (bool future : {false, true}) {
    card = {};
    card.disk["/battery"] = "";
    card.files = card.disk;
    BatteryJournal w("/battery");
    ASSERT_EQ(w.recover(true), JournalResult::Ok);
    ASSERT_EQ(w.append(sample(0)), JournalResult::Ok);
    for (auto& [path, bytes] : card.disk)
      if (path.ends_with(".bin")) bytes[future ? 3 : 18] = future ? '2' : 'X';
    card.restart();
    auto original = card.disk;
    BatteryJournal r("/battery");
    EXPECT_NE(r.recover(), JournalResult::Ok);
    EXPECT_NE(r.append(sample(1)), JournalResult::Ok);
    EXPECT_EQ(card.disk, original);
    EXPECT_EQ(r.futureVersion(), future);
  }
}
TEST_F(BatteryJournalTest, InvalidTelemetryCannotBecomeAKnownZero) {
  BatteryJournal w("/battery");
  ASSERT_EQ(w.recover(true), JournalResult::Ok);
  auto r = sample(0);
  r.pct = 101;
  EXPECT_EQ(w.append(r), JournalResult::Invalid);
  r = sample(0);
  r.kind = 0;
  EXPECT_EQ(w.append(r), JournalResult::Invalid);
  r = sample(0);
  r.pct = 0;
  ASSERT_EQ(w.append(r), JournalResult::Ok);
  battery::Record got;
  ASSERT_EQ(w.read(0, got), JournalResult::Ok);
  EXPECT_EQ(got.pct, 0);
}

TEST_F(BatteryJournalTest, CooperativeRecoveryDoesNotReadBeyondOneSegmentOrRetainAHandle) {
  BatteryJournal w("/battery", 128);
  ASSERT_EQ(w.recover(true), JournalResult::Ok);
  for (unsigned i = 0; i < 70; ++i) ASSERT_EQ(w.append(sample(i)), JournalResult::Ok);
  card.restart();
  BatteryJournal r("/battery", 128);
  ASSERT_EQ(r.startRecovery(), JournalResult::Incomplete);
  EXPECT_EQ(r.count(), 0u);
  EXPECT_EQ(r.recoverStep(), JournalResult::Incomplete);
  EXPECT_EQ(r.count(), 64u);
  EXPECT_EQ(card.handles, 0);
  EXPECT_EQ(r.recoverStep(), JournalResult::Incomplete);
  EXPECT_EQ(r.count(), 70u);
  EXPECT_EQ(card.handles, 0);
  EXPECT_EQ(r.recoverStep(), JournalResult::Incomplete);
  EXPECT_EQ(r.recoverStep(), JournalResult::Ok);
  EXPECT_EQ(card.handles, 0);
}
