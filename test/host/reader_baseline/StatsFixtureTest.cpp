// SPDX-License-Identifier: GPL-3.0-or-later
#include <BookReadingStats.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <vector>

namespace {
std::vector<uint8_t> fixture(const char* relative) {
  std::ifstream input(std::string(LEGACY_FIXTURES) + relative, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
constexpr char Cache[] = "/cache/book";
constexpr char Primary[] = "/cache/book/stats_v7.bin";
void seed(const char* target, const char* source) {
  auto file = Storage.open(target, O_RDWR | O_CREAT);
  file.close();
  Storage.data(target).bytes = fixture(source);
}
}  // namespace

TEST(LegacyReaderBaseline, LoadsActualSyntheticV7BytesAndPreservesCounterIdentity) {
  Storage.reset();
  seed(Primary, "/sd/.crosspoint/epub_305419896/stats_v7_crossink-020000000001.bin");
  auto stats = BookReadingStats::load(Cache);
  EXPECT_EQ(stats.sessionCount, 2);
  EXPECT_EQ(stats.totalReadingSeconds, 120u);
  EXPECT_EQ(stats.totalPagesTurned, 6u);
  EXPECT_EQ(stats.estimatedTimeLeftSeconds, 360u);
  for (size_t i = 0; i < 16; ++i) EXPECT_EQ(stats.counterId.bytes[i], i);
  stats.totalReadingSeconds += 20;
  ASSERT_TRUE(stats.save(Cache));
  const auto reopened = BookReadingStats::load(Cache);
  EXPECT_EQ(reopened.totalReadingSeconds, 140u);
  EXPECT_EQ(reopened.counterId.bytes, stats.counterId.bytes);
}

TEST(LegacyReaderBaseline, CorruptPrimaryRecoversCorpusBackupWithoutResettingTotals) {
  Storage.reset();
  seed(Primary, "/sd/.crosspoint/epub_305419896/stats_v7_crossink-020000000001.bin");
  seed("/cache/book/stats_v7.bin.bak", "/sd/.crosspoint/epub_305419896/stats_v7_crossink-020000000001.bin.bak");
  Storage.data(Primary).bytes[3] ^= 1;
  EXPECT_EQ(BookReadingStats::load(Cache).totalReadingSeconds, 120u);
}
