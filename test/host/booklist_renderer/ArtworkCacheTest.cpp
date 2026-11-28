#include <gtest/gtest.h>

#include "booksync/system/ArtworkCache.h"
using namespace booksync;
namespace {
struct ArtworkLibrary final : Library {
  mutable unsigned refs = 0, copies = 0;
  unsigned requests = 0;
  CoverReference reference{1, CoverWidth, CoverHeight};
  bool copyOkay = true;
  Capabilities capabilities() const override { return {}; }
  Snapshot snapshot() const override { return {}; }
  CommandStatus submit(Intent) override { return CommandStatus::Unavailable; }
  CoverReference coverReference(BookKey) const override {
    ++refs;
    return reference;
  }
  bool readCover(CoverReference r, CoverBitmap& out) const override {
    ++copies;
    out.width = r.width;
    out.height = r.height;
    return copyOkay;
  }
  CommandStatus requestCover(BookKey) override {
    ++requests;
    return CommandStatus::Unavailable;
  }
};
}  // namespace
TEST(ArtworkCache, OneCopyPerReferenceAndNoLibraryWorkFromPaintLookup) {
  ArtworkLibrary library;
  ArtworkCache cache;
  ASSERT_TRUE(cache.initArtworkCache(MemoryPool::None));
  BookKey key{{1, 2, 3, 0}, 4};
  ASSERT_TRUE(cache.pollArtworkKey(library, key));
  for (unsigned i = 0; i < 40; ++i) ASSERT_NE(cache.bitmapForArtwork(key), nullptr);
  EXPECT_EQ(library.refs, 1);
  EXPECT_EQ(library.copies, 1);
  EXPECT_FALSE(cache.pollArtworkKey(library, key));
  EXPECT_EQ(library.copies, 1);
  ++library.reference.id;
  EXPECT_TRUE(cache.pollArtworkKey(library, key));
  EXPECT_EQ(library.copies, 2);
}
TEST(ArtworkCache, MissingCorruptAndRetiredArtworkUsePlaceholder) {
  ArtworkLibrary library;
  ArtworkCache cache;
  ASSERT_TRUE(cache.initArtworkCache(MemoryPool::None));
  BookKey key;
  library.reference = {};
  EXPECT_FALSE(cache.pollArtworkKey(library, key));
  EXPECT_FALSE(cache.pollArtworkKey(library, key));
  EXPECT_EQ(library.requests, 1);
  EXPECT_EQ(cache.bitmapForArtwork(key), nullptr);
  library.reference = {2, 129, 192};
  EXPECT_FALSE(cache.pollArtworkKey(library, key));
  EXPECT_EQ(library.copies, 0);
  library.reference = {3, 128, 192};
  library.copyOkay = false;
  EXPECT_FALSE(cache.pollArtworkKey(library, key));
  EXPECT_FALSE(cache.pollArtworkKey(library, key));
  EXPECT_EQ(library.copies, 1);
  ++library.reference.id;
  library.copyOkay = true;
  ASSERT_TRUE(cache.pollArtworkKey(library, key));
  cache.retireArtworkEntries();
  EXPECT_EQ(cache.bitmapForArtwork(key), nullptr);
  cache.resetArtworkCache();
  EXPECT_FALSE(cache.pollArtworkKey(library, key));
}
TEST(ArtworkCache, CompleteVisibleBudgetAndDuplicateKeysStayBounded) {
  ArtworkLibrary library;
  ArtworkCache cache;
  ASSERT_TRUE(cache.initArtworkCache(MemoryPool::None));
  for (uint32_t i = 0; i < LibraryRows; ++i) ASSERT_TRUE(cache.pollArtworkKey(library, {{1, 2, 3, 0}, i}));
  EXPECT_EQ(library.copies, LibraryRows);
  EXPECT_FALSE(cache.pollArtworkKey(library, {{1, 2, 3, 0}, LibraryRows}));
  EXPECT_FALSE(cache.pollArtworkKey(library, {{1, 2, 3, 0}, 0}));
  EXPECT_EQ(library.copies, LibraryRows);
  EXPECT_EQ(cache.bitmapForArtwork({{1, 3, 3, 0}, 0}), nullptr);
}
TEST(ArtworkCache, FailedCapabilityAllocationNeverQueriesLibrary) {
  ArtworkLibrary library;
  ArtworkCache cache;
  EXPECT_FALSE(cache.initArtworkCache(MemoryPool::Psram));
  EXPECT_FALSE(cache.pollArtworkKey(library, {}));
  EXPECT_EQ(library.refs, 0);
  EXPECT_EQ(library.requests, 0);
}

TEST(ArtworkCache, PositionOnlyPublicationsReusePixelsAndRetiredKeysCannotPaint) {
  ArtworkLibrary library;ArtworkCache cache;ASSERT_TRUE(cache.initArtworkCache(MemoryPool::None));
  BookKey key{{1,2,3,0},4};ASSERT_TRUE(cache.pollArtworkKey(library,key));
  for(unsigned i=0;i<50;++i) {
    cache.beginArtworkPublication();cache.retainArtworkKey(key);cache.endArtworkPublication();
    EXPECT_FALSE(cache.pollArtworkKey(library,key));EXPECT_NE(cache.bitmapForArtwork(key),nullptr);
  }
  EXPECT_EQ(library.copies,1);
  auto retired=key;++key.provenance.accountEpoch;
  cache.beginArtworkPublication();cache.retainArtworkKey(key);cache.endArtworkPublication();
  EXPECT_EQ(cache.bitmapForArtwork(retired),nullptr);ASSERT_TRUE(cache.pollArtworkKey(library,key));EXPECT_EQ(library.copies,2);
}
