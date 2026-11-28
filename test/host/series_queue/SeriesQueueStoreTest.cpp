// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include <cstring>

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
}  // namespace
