// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include <fstream>
#include <iterator>

#include "EpubReaderUtils.h"
#include "booksync/reader/Locator.h"
using namespace booksync::reader;
namespace {
constexpr booksync::storage::SourceIdentity source{{{1}}, {{2}}, {{3}}};
std::string fixture() {
  std::ifstream in(std::string(LEGACY_FIXTURES) + "/sd/.crosspoint/epub_305419896/progress.bin", std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
}  // namespace
TEST(BookSyncLocator, FixtureReopensThroughNativeLoaderWithoutChangingBytes) {
  Storage.reset();
  const auto bytes = fixture();
  ASSERT_EQ(bytes.size(), 20u);
  const LocatorView locator{source, NativeEpubProgress, 1, bytes};
  progress_record::Fields anchor;
  ASSERT_EQ(epubAnchor(locator, source, anchor), ReplayStatus::Ready);
  EXPECT_EQ(anchor.spineIndex, 1);
  EXPECT_EQ(anchor.visibleTextOffset, 144u);
  EXPECT_TRUE(anchor.hasVisibleTextOffset);
  auto file = Storage.open("/epub/progress.bin", O_RDWR | O_CREAT);
  ASSERT_EQ(file.write(locator.raw.data(), locator.raw.size()), locator.raw.size());
  file.close();
  Epub epub;
  EpubReaderUtils::Progress reopened;
  ASSERT_TRUE(EpubReaderUtils::loadProgress(epub, reopened));
  EXPECT_EQ(reopened.visibleTextOffset, anchor.visibleTextOffset);
  EXPECT_EQ(reopened.spineIndex, anchor.spineIndex);
  EXPECT_EQ(locator.raw, bytes);
}
TEST(BookSyncLocator, XtcPageIsRawLittleEndianNotPercentOrEpubOffset) {
  const char raw[]{'\x04', '\x03', '\x02', '\x01'};
  const LocatorView locator{source, NativeXtcProgress, 1, {raw, sizeof(raw)}};
  uint32_t page = 0;
  ASSERT_EQ(xtcAnchor(locator, source, page), ReplayStatus::Ready);
  EXPECT_EQ(page, 0x01020304u);
  EXPECT_EQ(locator.raw.data(), raw);
}
TEST(BookSyncLocator, SourceRevisionAndEditionMustMatchBeforeReplay) {
  const auto raw = fixture();
  const LocatorView locator{source, NativeEpubProgress, 1, raw};
  for (int field = 0; field < 3; ++field) {
    auto other = source;
    if (field == 0) other.book.bytes[0]++;
    if (field == 1) other.edition.bytes[0]++;
    if (field == 2) other.revision.bytes[0]++;
    progress_record::Fields output;
    output.visibleTextOffset = 999;
    EXPECT_EQ(epubAnchor(locator, other, output), ReplayStatus::SourceMismatch);
    EXPECT_EQ(output.visibleTextOffset, 999u);
  }
}
TEST(BookSyncLocator, UnknownRawAnchorsAreRetainedAndNeverGuessed) {
  const char raw[]{'/', 'x', '\0', '\xff', ' '};
  const LocatorView locator{source, 60000, 99, {raw, sizeof(raw)}};
  EXPECT_TRUE(valid(locator));
  uint32_t page = 999;
  EXPECT_EQ(xtcAnchor(locator, source, page), ReplayStatus::Unsupported);
  EXPECT_EQ(page, 999u);
  EXPECT_EQ(locator.raw, std::string_view(raw, sizeof(raw)));
}
TEST(BookSyncLocator, CorruptTruncatedAndFutureRecordsDoNotResetPosition) {
  auto raw = fixture();
  raw[19] ^= 1;
  progress_record::Fields output;
  output.visibleTextOffset = 999;
  EXPECT_EQ(epubAnchor({source, NativeEpubProgress, 1, raw}, source, output), ReplayStatus::Invalid);
  EXPECT_EQ(epubAnchor({source, NativeEpubProgress, 2, raw}, source, output), ReplayStatus::Unsupported);
  EXPECT_EQ(epubAnchor({source, NativeEpubProgress, 1, raw.substr(0, 3)}, source, output), ReplayStatus::Invalid);
  EXPECT_EQ(output.visibleTextOffset, 999u);
  const std::string huge(MaxRawLocatorBytes + 1, 'x');
  EXPECT_FALSE(valid({source, NativeEpubProgress, 1, huge}));
}
TEST(BookSyncLocator, LegacyOffsetZeroAndAbsentAreDifferent) {
  const char raw[10]{};
  progress_record::Fields output;
  ASSERT_EQ(epubAnchor({source, NativeEpubProgress, 1, {raw, 10}}, source, output), ReplayStatus::Ready);
  EXPECT_TRUE(output.hasVisibleTextOffset);
  EXPECT_EQ(output.visibleTextOffset, 0u);
  ASSERT_EQ(epubAnchor({source, NativeEpubProgress, 1, {raw, 4}}, source, output), ReplayStatus::Ready);
  EXPECT_FALSE(output.hasVisibleTextOffset);
}
