// SPDX-License-Identifier: GPL-3.0-or-later
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <cstring>

#include "booksync/storage/PageJournal.h"
using namespace booksync::storage;
namespace {
PageEvent event(uint32_t n) { return {1, 1800000000ULL + n, n + 1, 10, 2, n, 500, 4, n, 10000, PageEvent::HasOffset}; }
struct PageJournalTest : testing::Test {
  PageJournal journal{"/pages"};
  void SetUp() override {
    card = {};
    ASSERT_EQ(journal.recover(), JournalResult::Ok);
  }
};
}  // namespace
TEST_F(PageJournalTest, AppendRestartAndSameObservationRetryKeepExactBytes) {
  for (unsigned i = 0; i < 70; ++i) ASSERT_EQ(journal.append(event(i)), JournalResult::Ok);
  ASSERT_EQ(journal.append(event(69)), JournalResult::Ok);
  card.restart();
  PageJournal restarted{"/pages"};
  ASSERT_EQ(restarted.recover(), JournalResult::Ok);
  EXPECT_EQ(restarted.count(), 70);
  for (unsigned i = 0; i < 70; ++i) {
    PageEvent e;
    ASSERT_EQ(restarted.read(i, e), JournalResult::Ok);
    EXPECT_EQ(e, event(i));
  }
}
TEST_F(PageJournalTest, CapReservesPermanentGapAndNeverEvictsUnsentRows) {
  PageJournal small{"/small", 2};
  ASSERT_EQ(small.recover(), JournalResult::Ok);
  ASSERT_EQ(small.append(event(0)), JournalResult::Ok);
  ASSERT_EQ(small.append(event(1)), JournalResult::Ok);
  EXPECT_EQ(small.append(event(2)), JournalResult::Capacity);
  card.restart();
  PageJournal restarted{"/small", 2};
  ASSERT_EQ(restarted.recover(), JournalResult::Ok);
  EXPECT_TRUE(restarted.capped());
  EXPECT_EQ(restarted.count(), 3);
  PageEvent e;
  ASSERT_EQ(restarted.read(2, e), JournalResult::Ok);
  EXPECT_EQ(e.flags, PageEvent::Gap);
  EXPECT_EQ(restarted.append(event(3)), JournalResult::Capacity);
  ASSERT_EQ(restarted.read(0, e), JournalResult::Ok);
  EXPECT_EQ(e, event(0));
}
TEST_F(PageJournalTest, EveryPublicationCutRecoversACompletePrefixAndRetryDoesNotDuplicate) {
  ASSERT_EQ(journal.append(event(0)), JournalResult::Ok);
  const auto seed = card.disk;
  card.events.clear();
  ASSERT_EQ(journal.append(event(1)), JournalResult::Ok);
  const auto operations = card.events.size();
  for (bool eager : {false, true})
    for (size_t cut = 0; cut < operations; ++cut) {
      SCOPED_TRACE(cut);
      card.disk = seed;
      card.restart();
      card.eager = eager;
      PageJournal writer{"/pages"};
      ASSERT_EQ(writer.recover(), JournalResult::Ok);
      card.events.clear();
      card.stop = cut;
      EXPECT_THROW(writer.append(event(1)), PowerCut);
      card.restart();
      PageJournal reboot{"/pages"};
      ASSERT_EQ(reboot.recover(), JournalResult::Ok);
      ASSERT_EQ(reboot.append(event(1)), JournalResult::Ok);
      EXPECT_EQ(reboot.count(), 2);
      PageEvent e;
      ASSERT_EQ(reboot.read(0, e), JournalResult::Ok);
      EXPECT_EQ(e, event(0));
    }
}
TEST_F(PageJournalTest, UnknownCorruptAndReadOnlySourcesAreRetained) {
  ASSERT_EQ(journal.append(event(0)), JournalResult::Ok);
  card.readOnly = true;
  EXPECT_NE(journal.append(event(1)), JournalResult::Ok);
  card.readOnly = false;
  for (auto& [path, bytes] : card.disk)
    if (path.ends_with(".bin")) bytes[0] = 'X';
  card.restart();
  PageJournal reboot{"/pages"};
  EXPECT_NE(reboot.recover(), JournalResult::Ok);
  EXPECT_FALSE(card.disk.empty());
}

TEST_F(PageJournalTest, FiftyThousandHistoryRowsUseBoundedReadsAndKeepTheCapGap) {
  // Fixture construction deliberately bypasses 50k fsyncs. Production recovery,
  // CRC decoding, random access and cap publication consume the resulting bytes.
  auto put = [](char* p, uint64_t n, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) p[i] = char(n >> (8 * i));
  };
  auto crc = [](const char* p, size_t bytes) {
    uint32_t n = ~0U;
    while (bytes--) {
      n ^= uint8_t(*p++);
      for (unsigned i = 0; i < 8; ++i) n = (n >> 1) ^ (0xedb88320U & (0U - (n & 1)));
    }
    return ~n;
  };
  card = {};
  for (uint32_t segment = 0; segment < 782; ++segment) {
    const uint32_t rows = std::min(64U, 50000 - segment * 64);
    std::string bytes(16 + rows * 64, '\0');
    memcpy(bytes.data(), "BSP1", 4);
    put(bytes.data() + 4, segment, 4);
    put(bytes.data() + 8, rows, 4);
    for (uint32_t row = 0; row < rows; ++row) {
      const auto n = segment * 64 + row;
      char* p = bytes.data() + 16 + row * 64;
      put(p, 1, 8);
      put(p + 8, 1800000000ULL + n, 8);
      put(p + 16, n + 1, 4);
      put(p + 20, 10, 4);
      put(p + 28, n, 4);
      put(p + 32, 50000, 4);
      put(p + 40, n, 4);
      put(p + 44, 100000, 4);
      put(p + 60, crc(p, 60), 4);
    }
    char path[64];
    snprintf(path, sizeof(path), "/pages/%06u.bin", segment);
    card.disk[path] = bytes;
  }
  card.restart();
  PageJournal history{"/pages"};
  ASSERT_EQ(history.recover(), JournalResult::Ok);
  ASSERT_EQ(history.count(), 50000);
  size_t operations = 0;
  for (uint32_t i = 0; i < 50000; ++i) {
    card.events.clear();
    PageEvent e;
    ASSERT_EQ(history.read(i, e), JournalResult::Ok);
    EXPECT_EQ(e.page, i);
    operations += card.events.size();
  }
  EXPECT_LT(operations, 4000000);
  EXPECT_LE(sizeof(PageJournal), 432);
  EXPECT_EQ(history.append(event(0)), JournalResult::Capacity);
  EXPECT_TRUE(history.capped());
  EXPECT_EQ(history.count(), 50001);
  PageEvent e;
  ASSERT_EQ(history.read(50000, e), JournalResult::Ok);
  EXPECT_EQ(e.flags, PageEvent::Gap);
}

TEST_F(PageJournalTest, TornStagingRowsAndFullCardNeverReplaceTheCommittedPrefix) {
  ASSERT_EQ(journal.append(event(0)), JournalResult::Ok);
  const auto seed = card.disk;
  for (int write : {0, 1, 2})
    for (int prefix : {0, 1, 8}) {
      card.disk = seed;
      card.restart();
      PageJournal writer{"/pages"};
      ASSERT_EQ(writer.recover(), JournalResult::Ok);
      card.writes = 0;
      card.cutWrite = write;
      card.prefix = prefix;
      EXPECT_THROW(writer.append(event(1)), PowerCut);
      card.restart();
      PageJournal reboot{"/pages"};
      ASSERT_EQ(reboot.recover(), JournalResult::Ok);
      ASSERT_EQ(reboot.count(), 1);
      ASSERT_EQ(reboot.append(event(1)), JournalResult::Ok);
      EXPECT_EQ(reboot.count(), 2);
    }
  card.disk = seed;
  card.restart();
  PageJournal writer{"/pages"};
  ASSERT_EQ(writer.recover(), JournalResult::Ok);
  card.full = true;
  EXPECT_EQ(writer.append(event(1)), JournalResult::IoError);
  card.full = false;
  card.restart();
  PageJournal reboot{"/pages"};
  ASSERT_EQ(reboot.recover(), JournalResult::Ok);
  EXPECT_EQ(reboot.count(), 1);
}

TEST_F(PageJournalTest, FutureBackupBlocksOlderWriterEvenWhenCurrentIsReadable) {
  ASSERT_EQ(journal.append(event(0)), JournalResult::Ok);
  ASSERT_EQ(journal.append(event(1)), JournalResult::Ok);
  for (auto& [path, bytes] : card.disk)
    if (path.ends_with(".bak")) bytes[3] = '9';
  const auto source = card.disk;
  card.restart();
  PageJournal reboot{"/pages"};
  EXPECT_EQ(reboot.recover(), JournalResult::RecoveryRequired);
  EXPECT_NE(reboot.append(event(2)), JournalResult::Ok);
  EXPECT_EQ(card.disk, source);
}
