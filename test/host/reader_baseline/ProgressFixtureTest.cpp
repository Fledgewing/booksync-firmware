// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "EpubReaderUtils.h"
#include <fstream>
#include <iterator>

namespace {
void seedProgress(const char* target, const char* fixture) {
  std::ifstream input(std::string(LEGACY_FIXTURES) + fixture, std::ios::binary);
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  auto file = Storage.open(target, O_RDWR | O_CREAT);
  file.close();
  Storage.data(target).bytes = std::move(bytes);
}
}  // namespace
TEST(LegacyReaderBaseline, ProgressFixtureKeepsVisibleAnchorAcrossReopenAndFailedSave) {
  Storage.reset();
  seedProgress("/epub/progress.bin", "/sd/.crosspoint/epub_305419896/progress.bin");
  Epub epub;
  EpubReaderUtils::Progress loaded;
  ASSERT_TRUE(EpubReaderUtils::loadProgress(epub, loaded));
  EXPECT_EQ(loaded.spineIndex, 1);
  EXPECT_EQ(loaded.pageNumber, 3);
  EXPECT_EQ(loaded.pageCount, 12);
  EXPECT_TRUE(loaded.hasVisibleTextOffset);
  EXPECT_EQ(loaded.visibleTextOffset, 144u);
  auto backup = Storage.open("/epub/progress.bin.bak", O_RDWR | O_CREAT);
  backup.close();
  Storage.data("/epub/progress.bin.bak").writeLimit = 5;
  EXPECT_FALSE(EpubReaderUtils::saveProgress(epub, 1, 4, 12, 192));
  ASSERT_TRUE(EpubReaderUtils::loadProgress(epub, loaded));
  EXPECT_EQ(loaded.visibleTextOffset, 144u);
}
TEST(LegacyReaderBaseline, TornCorpusProgressFallsBackToOlderCompleteAnchor) {
  Storage.reset();
  seedProgress("/epub/progress.bin", "/variants/progress/torn.bin");
  seedProgress("/epub/progress.bin.bak", "/sd/.crosspoint/epub_305419896/progress.bin.bak");
  Epub epub;
  EpubReaderUtils::Progress loaded;
  ASSERT_TRUE(EpubReaderUtils::loadProgress(epub, loaded));
  EXPECT_EQ(loaded.pageNumber, 2);
  EXPECT_EQ(loaded.visibleTextOffset, 96u);
}
