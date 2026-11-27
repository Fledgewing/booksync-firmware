// SPDX-License-Identifier: GPL-3.0-or-later
#include <HalStorage.h>
#include <gtest/gtest.h>
#include <uzlib.h>
#include <fstream>
#include <iterator>
#include "ClippingStore.h"
#include "activities/reader/ReadingStatsUtils.h"

bool ReadingStatsDate::isValid() const { return false; }
uint32_t readingStatsDayIndex(const ReadingStatsDate&) { return 0; }
void SyncPending::SessionMarker::mark(const std::string&, uint8_t) {}

TEST(LegacyReaderBaseline, HighlightFixtureRetainsSourceRangeAndPendingTombstone) {
  auto& store = ClippingStore::getInstance();
  store.unload();
  fake::reset();
  const std::string book = "/Books/Synthetic.epub";
  const std::string path = "/.crosspoint/clippings/epub_" +
      std::to_string(uzlib_crc32(book.data(), book.size(), 0)) + ".bin";
  std::ifstream input(std::string(LEGACY_FIXTURES) +
      "/sd/.crosspoint/clippings/epub_0123456789abcdef0123456789abcdef.bin", std::ios::binary);
  const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  ASSERT_FALSE(bytes.empty());
  // The retained baseline uses path-keyed stores; bytes are identical to the fork's content-keyed stores.
  fake::add(path, bytes);
  ASSERT_TRUE(store.loadForBook(book, "Synthetic Book", "Synthetic Author", "epub"));
  ASSERT_EQ(store.clippingCount(), 1u);
  EXPECT_EQ(store.getClippings()[0].sourceStart, 144u);
  EXPECT_EQ(store.getClippings()[0].sourceEnd, 158u);
  ASSERT_TRUE(store.saveToFile());
  store.unload();
  ASSERT_TRUE(store.loadForBook(book, "Synthetic Book", "Synthetic Author", "epub"));
  EXPECT_EQ(store.getClippings()[0].sourceStart, 144u);
  const auto& saved = fake::files.at(path)->bytes;
  const std::string serialized(saved.begin(), saved.end());
  EXPECT_NE(serialized.find("0123456789abcdefsynthetic-deletion"), std::string::npos);
  store.unload();
}
