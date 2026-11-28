// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>

#include "booksync/storage/SeriesQueueStore.h"
using namespace booksync;
using namespace booksync::storage;
namespace {
constexpr PublishPaths paths{"/queues", "/queues.tmp", "/queues.bak"};
struct QueueStoreTest : testing::Test {
  SeriesQueueStore store{paths, "/series", {"server", "account"}};
  SeriesQueueRecord record{}, result{};
  SeriesQueueIndex index{}, recovered{};
  void SetUp() override {
    card = {};
    record.id = record.sequence = record.token = 1;
    record.count = 3;
    std::strcpy(record.request.key.data(), "series");
    for (unsigned i = 0; i < record.count; ++i) {
      auto& r = record.books[i];
      std::strcpy(r.file.server.data(), "server");
      std::strcpy(r.file.account.data(), "account");
      std::strcpy(r.file.book.data(), "book");
      std::strcpy(r.file.file.data(), "file");
      std::strcpy(r.file.variant.data(), "epub");
      std::strcpy(r.file.path.data(), "/books/title.epub");
      std::strcpy(r.title.data(), "Title");
      std::strcpy(r.variantLabel.data(), "EPUB");
      r.key.ordinal = i;
      r.file.bytes = 123;
      r.file.sizeKnown = true;
    }
    index.sequence = index.counter = 1;
    index.slots[0].id = 1;
    std::strcpy(index.slots[0].series.data(), "series");
  }
};
TEST_F(QueueStoreTest, FreshIndexHasNoQueues) {
  ASSERT_TRUE(store.recover(recovered));
  EXPECT_EQ(recovered.counter, 0u);
}
TEST_F(QueueStoreTest, RoundTripsExactSelectionAndWireLengths) {
  ASSERT_TRUE(store.save(0, record));
  ASSERT_TRUE(store.saveIndex(index));
  EXPECT_EQ(card.files["/series.0"].size(), SeriesQueueStore::RecordBase + 3 * SeriesQueueStore::BookBytes);
  EXPECT_EQ(card.files["/queues"].size(), SeriesQueueStore::IndexBytes);
  card.restart();
  ASSERT_TRUE(store.recover(recovered));
  ASSERT_TRUE(store.load(0, 1, result));
  EXPECT_EQ(recovered.slots[0].id, 1u);
  EXPECT_EQ(result.count, 3u);
  EXPECT_EQ(result.books[2].file, record.books[2].file);
  EXPECT_EQ(result.books[2].key.ordinal, 2u);
  EXPECT_EQ(card.handles, 0);
  EXPECT_EQ(card.maxHandles, 1);
}
TEST_F(QueueStoreTest, SupportsAllTenAndEveryRetainedSlot) {
  record.count = 10;
  record.request.count = 10;
  for (unsigned i = 3; i < 10; ++i) record.books[i] = record.books[0];
  for (unsigned slot = 0; slot < 4; ++slot) {
    record.id = slot + 1;
    ASSERT_TRUE(store.save(slot, record));
    ASSERT_TRUE(store.load(slot, slot + 1, result));
    EXPECT_EQ(result.count, 10);
  }
  EXPECT_FALSE(store.save(4, record));
}
TEST_F(QueueStoreTest, AccountAndServerChangesCannotReadOrReplace) {
  ASSERT_TRUE(store.save(0, record));
  ASSERT_TRUE(store.saveIndex(index));
  auto before = card.files;
  SeriesQueueStore account{paths, "/series", {"server", "other"}};
  EXPECT_FALSE(account.recover(recovered));
  EXPECT_FALSE(account.load(0, 1, result));
  EXPECT_FALSE(account.save(0, record));
  SeriesQueueStore server{paths, "/series", {"other", "account"}};
  EXPECT_FALSE(server.recover(recovered));
  EXPECT_FALSE(server.load(0, 1, result));
  EXPECT_EQ(card.files, before);
}
TEST_F(QueueStoreTest, MalformedCountsAndStringsAndEnumsFailBeforeWrite) {
  record.count = 0;
  EXPECT_FALSE(store.save(0, record));
  record.count = 11;
  EXPECT_FALSE(store.save(0, record));
  record.count = 3;
  record.books[0].title.fill('x');
  EXPECT_FALSE(store.save(0, record));
  record.books[0].title.fill(0);
  record.books[0].state = static_cast<SeriesRowState>(255);
  EXPECT_FALSE(store.save(0, record));
  EXPECT_TRUE(card.files.empty());
}
TEST_F(QueueStoreTest, UnknownFormatsAndDamagedBytesArePreserved) {
  ASSERT_TRUE(store.save(0, record));
  card.files["/series.0"][4] = 2;
  auto before = card.files;
  EXPECT_FALSE(store.load(0, 1, result));
  EXPECT_FALSE(store.save(0, record));
  EXPECT_EQ(card.files, before);
}
TEST_F(QueueStoreTest, WrongIdCannotLoadAnotherQueue) {
  ASSERT_TRUE(store.save(0, record));
  EXPECT_FALSE(store.load(0, 2, result));
}
TEST_F(QueueStoreTest, TerminalSlotReuseRetainsReferencedOldBackup) {
  ASSERT_TRUE(store.save(0, record));
  record.id = 2;
  ++record.sequence;
  ASSERT_TRUE(store.save(0, record));
  ASSERT_TRUE(store.load(0, 1, result));
  EXPECT_EQ(result.id, 1u);
  ASSERT_TRUE(store.load(0, 2, result));
  EXPECT_EQ(result.id, 2u);
}
TEST_F(QueueStoreTest, MissingCurrentRecoversCompleteBackup) {
  ASSERT_TRUE(store.save(0, record));
  ++record.sequence;
  ASSERT_TRUE(store.save(0, record));
  card.files.erase("/series.0");
  ASSERT_TRUE(store.load(0, 1, result));
  EXPECT_EQ(result.sequence, 1u);
}
TEST_F(QueueStoreTest, UnreferencedStagingIsNeverLoaded) {
  ASSERT_TRUE(store.save(0, record));
  card.files["/series.0.tmp"] = card.files["/series.0"];
  card.files.erase("/series.0");
  EXPECT_FALSE(store.load(0, 1, result));
}
TEST_F(QueueStoreTest, DirtyStringPaddingIsNormalized) {
  record.books[0].title[80] = 'x';
  ASSERT_TRUE(store.save(0, record));
  ASSERT_TRUE(store.load(0, 1, result));
  EXPECT_EQ(result.books[0].title[80], 0);
}
TEST_F(QueueStoreTest, EveryPowerCutRetainsOnlyOldOrNewCompleteRecord) {
  ASSERT_TRUE(store.save(0, record));
  const auto baseline = card;
  ++record.sequence;
  record.books[1].state = SeriesRowState::Complete;
  card.events.clear();
  ASSERT_TRUE(store.save(0, record));
  const int events = card.events.size();
  for (int cut = 0; cut < events; ++cut) {
    card = baseline;
    card.events.clear();
    card.stop = cut;
    try {
      store.save(0, record);
    } catch (const PowerCut&) {
    }
    card.restart();
    ASSERT_TRUE(store.load(0, 1, result)) << cut;
    ASSERT_TRUE(result.sequence == 1 || result.sequence == 2) << cut;
    EXPECT_EQ(result.books[1].state, result.sequence == 1 ? SeriesRowState::Queued : SeriesRowState::Complete) << cut;
    EXPECT_EQ(card.handles, 0) << cut;
  }
}
TEST_F(QueueStoreTest, EveryReportedIoFailureNeverAcknowledgesOldRecordAsNew) {
  ASSERT_TRUE(store.save(0, record));
  const auto baseline = card;
  ++record.sequence;
  card.events.clear();
  ASSERT_TRUE(store.save(0, record));
  const int events = card.events.size();
  for (int failure = 0; failure < events; ++failure) {
    card = baseline;
    card.events.clear();
    card.fail = failure;
    const bool saved = store.save(0, record);
    card.fail = -1;
    if (saved) {
      ASSERT_TRUE(store.load(0, 1, result));
      EXPECT_EQ(result.sequence, 2u) << failure;
    }
    EXPECT_EQ(card.handles, 0) << failure;
  }
}
TEST_F(QueueStoreTest, InvalidIndexCannotEvictReferences) {
  index.slots[1] = index.slots[0];
  EXPECT_FALSE(store.saveIndex(index));
  index.slots[1] = {};
  index.slots[0].id = 2;
  EXPECT_FALSE(store.saveIndex(index));
  EXPECT_TRUE(card.files.empty());
}
TEST_F(QueueStoreTest, FullCardDoesNotStartPublication) {
  card.full = true;
  EXPECT_FALSE(store.save(0, record));
  EXPECT_TRUE(card.files.empty());
}
TEST_F(QueueStoreTest, MatchingChecksumNeverAcknowledgesAnotherValidRecord) {
  ASSERT_TRUE(store.save(0, record));
  const auto original = card.files["/series.0"];
  constexpr size_t TitleOffset = 292 + 762 + 28;
  uint32_t prefix = 2166136261u;
  for (size_t i = 0; i < TitleOffset; ++i) prefix = (prefix ^ static_cast<uint8_t>(original[i])) * 16777619u;
  std::unordered_map<uint32_t, uint32_t> seen;
  seen.reserve(200000);
  uint32_t first = 0, second = 0;
  bool found = false;
  for (uint32_t i = 0; i < 1000000 && !found; ++i) {
    char text[9];
    std::snprintf(text, sizeof(text), "%08x", unsigned(i));
    uint32_t hash = prefix;
    for (unsigned j = 0; j < 8; ++j) hash = (hash ^ static_cast<uint8_t>(text[j])) * 16777619u;
    const auto inserted = seen.emplace(hash, i);
    if (!inserted.second) {
      first = inserted.first->second;
      second = i;
      found = true;
    }
  }
  ASSERT_TRUE(found);
  std::snprintf(record.books[0].title.data(), 96, "%08x", unsigned(first));
  ASSERT_TRUE(store.save(0, record));
  const auto baseline = card;
  const auto old = card.files["/series.0"];
  std::snprintf(record.books[0].title.data(), 96, "%08x", unsigned(second));
  card.events.clear();
  ASSERT_TRUE(store.save(0, record));
  ASSERT_EQ(old.substr(old.size() - 4), card.files["/series.0"].substr(old.size() - 4));
  ASSERT_NE(old, card.files["/series.0"]);
  const auto fault = std::find(card.events.begin(), card.events.end(), "file-sync:before") - card.events.begin();
  ASSERT_LT(size_t(fault), card.events.size());
  card = baseline;
  card.events.clear();
  card.fail = fault;
  EXPECT_FALSE(store.save(0, record));
  EXPECT_EQ(card.files["/series.0"], old);
}
}  // namespace
