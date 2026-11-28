#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "booksync/services/PersonalWorker.h"
#include "booksync/services/SavedNativePassages.h"
using namespace booksync;
using namespace booksync::storage;
namespace {
void u16(std::string& s, uint16_t n) {
  s.push_back(char(n));
  s.push_back(char(n >> 8));
}
void u32(std::string& s, uint32_t n) {
  u16(s, uint16_t(n));
  u16(s, uint16_t(n >> 16));
}
void field(std::string& s, const std::string& value) {
  u32(s, value.size());
  s += value;
}
class NativeFileTest : public testing::Test {
 protected:
  Index index{{"/idx", "/idx.tmp", "/idx.bak"}, "/data-a", "/data-b"};
  SavedNativePassages saved{index};
  PersonalRecord out{};
  static constexpr const char* File = "/.crosspoint/clippings/epub_2078032738.bin";  // Independent CRC32 golden.
  void SetUp() override {
    card = {};
    ASSERT_TRUE(index.begin({7, 1, 0, 9}));
    IndexRecord row;
    std::strcpy(row.path.data(), "/books/one.epub");
    std::strcpy(row.title.data(), "Saved book");
    ASSERT_TRUE(index.append(row));
    ASSERT_TRUE(index.commit());
    card.events.clear();
  }
  std::string fixture(unsigned version = 7, std::string path = "/books/one.epub", std::string text = "A saved passage.",
                      uint8_t flags = 0, std::string key = "saved-id") {
    std::string s(1, char(version));
    u16(s, 1);
    field(s, "Original title");
    field(s, "Original author");
    field(s, path);
    if (version >= 5) u16(s, 0);
    for (unsigned n : {2, 3, 4, 100, 5, 6, 7, 8}) u16(s, n);
    u32(s, 1700000000);
    if (version >= 3) u32(s, 0x1234);
    if (version >= 4) u16(s, 0xffff);
    if (version >= 7) {
      u32(s, 100);
      u32(s, 200);
    }
    if (version >= 5) {
      field(s, key);
      s.push_back(char(flags));
    }
    std::string chapter = "Saved chapter";
    chapter.resize(48, 0);
    s += chapter;
    if (version == 1)
      u32(s, text.size());
    else
      u16(s, text.size());
    s += text;
    return s;
  }
  NativePassageStep read(uint16_t ordinal = 0) {
    const auto p = saved.ports();
    return p.passage(p.context, 0, ordinal, out);
  }
};
TEST_F(NativeFileTest, AllSevenRetainedFormatsAreReadWithoutWrites) {
  for (unsigned v = 1; v <= 7; ++v) {
    card.files[File] = fixture(v);
    card.events.clear();
    ASSERT_EQ(read(), NativePassageStep::Record) << v;
    EXPECT_STREQ(out.passage.data(), "A saved passage.");
    EXPECT_STREQ(out.source.data(), "Saved chapter");
    EXPECT_STREQ(out.book.title.data(), "Saved book");
    EXPECT_EQ(out.book.key.provenance.generation, 7u);
    EXPECT_EQ(card.handles, 0);
    for (const auto& event : card.events) EXPECT_EQ(event.find("write"), std::string::npos);
  }
}
TEST_F(NativeFileTest, OriginalKeysAndCoordinatesRemainExact) {
  card.files[File] = fixture();
  ASSERT_EQ(read(), NativePassageStep::Record);
  EXPECT_STREQ(out.identity.data(), "native:15:/books/one.epub:key:saved-id");
  card.files[File] = fixture(1);
  ASSERT_EQ(read(), NativePassageStep::Record);
  EXPECT_STREQ(out.identity.data(), "native:15:/books/one.epub:range:2:3:4:100:5:6:7:8:1700000000");
}
TEST_F(NativeFileTest, FilenameHashCannotAuthorizeAnotherPath) {
  card.files[File] = fixture(7, "/books/other.epub");
  EXPECT_EQ(read(), NativePassageStep::Error);
  EXPECT_EQ(card.handles, 0);
}
TEST_F(NativeFileTest, ScopedRemoteRecordIsExcludedFromNativeOwnership) {
  card.files[File] = fixture(7, "/books/one.epub", "Saved remote passage", 2, std::string(16, 's') + "remote-id");
  EXPECT_EQ(read(), NativePassageStep::Skip);
}
TEST_F(NativeFileTest, MissingAndCompleteEmptyAreDistinct) {
  EXPECT_EQ(read(), NativePassageStep::Missing);
  std::string empty(1, 7);
  u16(empty, 0);
  field(empty, "");
  field(empty, "");
  field(empty, "/books/one.epub");
  u16(empty, 0);
  card.files[File] = empty;
  EXPECT_EQ(read(), NativePassageStep::End);
}
TEST_F(NativeFileTest, BackupOnlyWhenCurrentAbsentAndNeverRepairOriginal) {
  card.files[std::string(File) + ".bak"] = fixture();
  EXPECT_EQ(read(), NativePassageStep::Record);
  card.files[File] = "torn";
  auto before = card.files;
  EXPECT_EQ(read(), NativePassageStep::Error);
  EXPECT_EQ(card.files, before);
}
TEST_F(NativeFileTest, EveryTruncationRejectsSelectedRecordAndClosesFile) {
  const auto full = fixture();
  for (size_t n = 0; n < full.size(); ++n) {
    card.files[File] = full.substr(0, n);
    EXPECT_EQ(read(), NativePassageStep::Error) << n;
    EXPECT_EQ(card.handles, 0) << n;
  }
}
TEST_F(NativeFileTest, OversizedPassageAndFileAreRefused) {
  card.files[File] = fixture(7, "/books/one.epub", std::string(901, 'x'));
  EXPECT_EQ(read(), NativePassageStep::Oversized);
  card.files[File] = std::string(2 * 1024 * 1024 + 1, 'x');
  EXPECT_EQ(read(), NativePassageStep::Oversized);
  EXPECT_EQ(card.handles, 0);
}
TEST_F(NativeFileTest, MalformedUnicodeAndEmbeddedNulAreNotPublished) {
  card.files[File] = fixture(7, "/books/one.epub", std::string("a\0b", 3));
  EXPECT_EQ(read(), NativePassageStep::Error);
  card.files[File] = fixture(7, "/books/one.epub", std::string("\xc0\x80", 2));
  EXPECT_EQ(read(), NativePassageStep::Error);
  card.files[File] = fixture(7, "/books/one.epub", "A real \xe7\x81\xaf\xe7\xac\xbc");
  EXPECT_EQ(read(), NativePassageStep::Record);
}
TEST_F(NativeFileTest, EndAfterLastNativeOrdinal) {
  card.files[File] = fixture();
  EXPECT_EQ(read(1), NativePassageStep::End);
}
TEST_F(NativeFileTest, InvalidSourceRangeIsRejected) {
  auto bytes = fixture();  // Source range starts after header + 20 base + 4 layout + 2 table.
  const auto offset = 3 + 4 + 14 + 4 + 15 + 4 + 15 + 2 + 20 + 4 + 2;
  ASSERT_LT(offset + 8, bytes.size());
  bytes[offset] = char(200);
  bytes[offset + 4] = char(100);
  card.files[File] = bytes;
  EXPECT_EQ(read(), NativePassageStep::Error);
}
TEST_F(NativeFileTest, WorkerPublishesSavedHighlightOnlyAfterBoundedSelection) {
  card.files[File] = fixture();
  PersonalWorker worker(index);
  const ClockStatus clock{ClockEvidence::Synchronized, 1795789920, 0, {2026, 11, 27, 5, 14, 32, 0}};
  ASSERT_TRUE(worker.begin(1, 0, clock));
  EXPECT_TRUE(worker.busy());
  EXPECT_EQ(worker.result().highlight.status.state, PersonalCardState::NotSaved);
  unsigned ticks = 0;
  while (worker.busy() && ticks++ < 20) ASSERT_TRUE(worker.tick());
  EXPECT_LT(ticks, 20u);
  const auto& result = worker.result();
  EXPECT_GT(result.publication, 0u);
  EXPECT_EQ(result.highlight.status.state, PersonalCardState::Ready);
  EXPECT_EQ(result.highlight.status.total, 1u);
  EXPECT_TRUE(result.highlight.canOpen);
  EXPECT_EQ(result.highlight.status.origin, PersonalOrigin::ThisReader);
  EXPECT_STREQ(result.highlight.passage.data(), "A saved passage.");
  worker.retire();
  EXPECT_EQ(worker.result().publication, 0u);
}
TEST_F(NativeFileTest, UnknownClockNeverSelectsFallbackDate) {
  card.files[File] = fixture();
  PersonalWorker worker(index);
  card.events.clear();
  ASSERT_TRUE(worker.begin(1, 0, {}));
  EXPECT_FALSE(worker.busy());
  EXPECT_EQ(worker.result().highlight.status.state, PersonalCardState::NotSaved);
  EXPECT_TRUE(card.events.empty());
}
TEST_F(NativeFileTest, DiscoverBudgetChargesEveryBookAndFullPayload) {
  EXPECT_EQ(personalWidgetRows(8), 4);
  EXPECT_EQ(personalWidgetRows(15), 7);
  EXPECT_GE(personalWidgetBytes(8), 4 * sizeof(BookRow) + sizeof(PersonalStatus));
  EXPECT_GE(personalWidgetBytes(15),
            sizeof(HighlightWidget) + sizeof(YearAgoWidget) + sizeof(RecommendedWidget) + sizeof(DiscoverWidget));
  EXPECT_EQ(personalWidgetBytes(0), 0);
}
}  // namespace

TEST_F(NativeFileTest, NativeCloseFailureRefusesTheRecord) {
  card.files[File]=fixture(); card.events.clear(); ASSERT_EQ(read(),NativePassageStep::Record);
  std::vector<int> closes; for(unsigned i=0;i<card.events.size();++i) if(card.events[i]=="close") closes.push_back(i);
  ASSERT_GE(closes.size(),2u); const auto nativeClose=closes[1]; card.events.clear(); card.fail=nativeClose;
  EXPECT_EQ(read(),NativePassageStep::Error); EXPECT_EQ(card.handles,0);
}
