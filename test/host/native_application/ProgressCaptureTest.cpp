#include <HalStorage.h>
#include <gtest/gtest.h>

#include "booksync/reader/Capture.h"
#include "booksync/server/bookorbit/ProgressCapture.h"
using namespace booksync::storage;
using namespace booksync::server;
namespace {
constexpr ScopeView scope{"server-a", "account-a"};
constexpr SourceIdentity identity{{{1}}, {{2}}, {{3}}};
constexpr PublishPaths paths{"/progress-capture", "/progress-capture.tmp", "/progress-capture.bak"};
class ProgressCaptureTest : public testing::Test {
 protected:
  std::array<char, 8192> scratch{};
  std::string payload;
  FrozenProgress record;
  void SetUp() override {
    card = {};
    LifecycleJournal journal{{"/native", "/native.tmp", "/native.bak"}};
    ASSERT_EQ(journal.recover(), JournalResult::Ok);
    booksync::reader::Capture capture(journal);
    ASSERT_EQ(capture.open("/book.epub", 1, {identity, scope, "opaque"}), JournalResult::Ok);
    JournalEntry entry;
    ASSERT_EQ(journal.read(capture.id(), entry, scratch.data(), scratch.size()), JournalResult::Ok);
    payload = entry.payload;
    record = {scope,   identity,
              900,     capture.id(),
              payload, {true, true, .25, 200, 77, 3, std::string_view("raw\0native", 10), "local", false, false},
              false};
    record.document = "native-document";
  }
  bool load(FrozenProgress& out) {
    ProgressCapture capture(paths, scratch.data(), scratch.size());
    return capture.load(scope, identity, record.entry, record.nativeOpen, payload, record.document, out);
  }
};
}  // namespace
TEST_F(ProgressCaptureTest, FreezesOriginalRawBytesAndRestoresAfterRestart) {
  ProgressCapture capture(paths, scratch.data(), scratch.size());
  ASSERT_TRUE(capture.freeze(record));
  card.restart();
  FrozenProgress restored;
  ASSERT_TRUE(load(restored));
  EXPECT_EQ(restored.value, record.value);
  EXPECT_EQ(restored.payload, payload);
  EXPECT_EQ(restored.source, identity);
  EXPECT_FALSE(restored.deliver);
  EXPECT_EQ(card.handles, 0);
}
TEST_F(ProgressCaptureTest, CannotRecaptureOrReplacePublishedIntent) {
  ProgressCapture capture(paths, scratch.data(), scratch.size());
  ASSERT_TRUE(capture.freeze(record));
  const auto original = card.files.at(paths.current);
  ASSERT_TRUE(capture.freeze(record));
  auto changed = record;
  changed.deliver = true;
  EXPECT_FALSE(capture.freeze(changed));
  changed = record;
  changed.value.raw = "new native place";
  EXPECT_FALSE(capture.freeze(changed));
  EXPECT_EQ(card.files.at(paths.current), original);
}
TEST_F(ProgressCaptureTest, ExactAccountSourceEntryAndOriginalOpenAreRequired) {
  ProgressCapture capture(paths, scratch.data(), scratch.size());
  ASSERT_TRUE(capture.freeze(record));
  FrozenProgress out;
  EXPECT_FALSE(capture.load({"server-a", "account-b"}, identity, record.entry, record.nativeOpen, payload,
                            record.document, out));
  auto other = identity;
  other.edition.bytes[0] = 9;
  EXPECT_FALSE(capture.load(scope, other, record.entry, record.nativeOpen, payload, record.document, out));
  EXPECT_FALSE(capture.load(scope, identity, record.entry + 1, record.nativeOpen, payload, record.document, out));
  EXPECT_FALSE(capture.load(scope, identity, record.entry, record.nativeOpen + 1, payload, record.document, out));
  EXPECT_FALSE(capture.load(scope, identity, record.entry, record.nativeOpen, "BSO1", record.document, out));
  EXPECT_FALSE(capture.load(scope, identity, record.entry, record.nativeOpen, payload, "other-document", out));
}
TEST_F(ProgressCaptureTest, EveryPublicationCutRetainsOnlyTheFrozenIntent) {
  ProgressCapture capture(paths, scratch.data(), scratch.size());
  const auto before = card;
  card.events.clear();
  ASSERT_TRUE(capture.freeze(record));
  const auto steps = card.events.size();
  for (bool eager : {false, true})
    for (size_t cut = 0; cut < steps; ++cut) {
      card = before;
      card.events.clear();
      card.eager = eager;
      card.stop = static_cast<int>(cut);
      try {
        capture.freeze(record);
      } catch (const PowerCut&) {
      }
      card.restart();
      FrozenProgress restored;
      if (load(restored)) {
        EXPECT_EQ(restored.value, record.value) << cut;
        EXPECT_EQ(restored.payload, payload) << cut;
      }
      EXPECT_EQ(card.handles, 0) << cut;
    }
}
TEST_F(ProgressCaptureTest, TruncatedUnknownAndCorruptAuthorityCannotBeReplaced) {
  ProgressCapture capture(paths, scratch.data(), scratch.size());
  ASSERT_TRUE(capture.freeze(record));
  const auto original = card.files.at(paths.current);
  for (size_t size = 0; size < original.size(); ++size) {
    card.files[paths.current] = original.substr(0, size);
    FrozenProgress output;
    EXPECT_FALSE(load(output)) << size;
    EXPECT_FALSE(capture.freeze(record)) << size;
    EXPECT_EQ(card.files.at(paths.current), original.substr(0, size)) << size;
  }
  card.files[paths.current] = original;
  card.files[paths.current][3] = '9';
  FrozenProgress output;
  EXPECT_FALSE(load(output));
  EXPECT_FALSE(capture.freeze(record));
  card.files[paths.current] = original;
  card.files[paths.current][44] ^= 1;
  EXPECT_FALSE(load(output));
  EXPECT_FALSE(capture.freeze(record));
}
TEST_F(ProgressCaptureTest, RejectsUnboundCaptureAndOversizedValuesWithoutTruncation) {
  ProgressCapture capture(paths, scratch.data(), scratch.size());
  auto invalid = record;
  invalid.payload = "BSO1";
  EXPECT_FALSE(capture.freeze(invalid));
  invalid = record;
  std::string large(4097, 'x');
  invalid.value.raw = large;
  EXPECT_FALSE(capture.freeze(invalid));
  invalid = record;
  invalid.nativeOpen = 0;
  EXPECT_FALSE(capture.freeze(invalid));
  EXPECT_FALSE(card.files.count(paths.current));
}
