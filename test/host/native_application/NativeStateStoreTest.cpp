#include <HalStorage.h>
#include <gtest/gtest.h>

#include "booksync/reader/Capture.h"
#include "booksync/server/bookorbit/NativeStateStore.h"
using namespace booksync::storage;
using namespace booksync::server;
namespace {
constexpr ScopeView stateScope{"server-a", "account-a"};
constexpr SourceIdentity stateSource{{{1}}, {{2}}, {{3}}};
constexpr PublishPaths statePaths{"/state", "/state.tmp", "/state.bak"};
constexpr PublishPaths stateCapturePaths{"/state.capture", "/state.capture.tmp", "/state.capture.bak"};
class NativeStateStoreTest : public testing::Test {
 protected:
  std::array<char, NativeStateStore::MaxBytes> scratch{}, captureScratch{};
  NativeStateStore store{statePaths, scratch.data(), scratch.size()};
  NativeStateStore capture{stateCapturePaths, captureScratch.data(), captureScratch.size()};
  NativeStateDocument document;
  std::string payload;
  void SetUp() override {
    card = {};
    document.scope = stateScope;
    document.source = stateSource;
    document.token = 5;
    document.local = {true, "reading", "2026-11-28", true, 4, true, true, "native review"};
    document.history = {false, "future-status", "2026-11-27", true, 3, true, false, {}};
    document.reviewModified = "2026-11-28";
    LifecycleJournal journal{{"/native", "/native.tmp", "/native.bak"}};
    ASSERT_EQ(journal.recover(), JournalResult::Ok);
    booksync::reader::Capture producer(journal);
    ASSERT_EQ(producer.open("/book.epub", 1, {stateSource, stateScope, "opaque"}), JournalResult::Ok);
    JournalEntry entry;
    ASSERT_EQ(journal.read(producer.id(), entry, scratch.data(), scratch.size()), JournalResult::Ok);
    payload = entry.payload;
  }
  NativeStateDocument frozen() {
    auto result = document;
    result.entry = 900;
    result.nativeOpen = 1;
    result.payload = payload;
    result.document = "native-document";
    return result;
  }
};
}  // namespace
TEST_F(NativeStateStoreTest, NativeValuesAndSeparateKnownHistorySurviveRestart) {
  ASSERT_TRUE(store.save(document));
  card.restart();
  NativeStateDocument loaded;
  ASSERT_EQ(store.load(stateScope, stateSource, loaded), NativeStateLoad::Ready);
  EXPECT_EQ(loaded.token, 5);
  EXPECT_EQ(loaded.local.status, "reading");
  EXPECT_EQ(loaded.local.reviewNote, "native review");
  EXPECT_FALSE(loaded.history.statusKnown);
  EXPECT_EQ(loaded.history.status, "future-status");
  EXPECT_TRUE(loaded.history.reviewKnown);
  EXPECT_FALSE(loaded.history.reviewSet);
  EXPECT_EQ(loaded.reviewModified, "2026-11-28");
}
TEST_F(NativeStateStoreTest, MissingProjectionCanInitializeUsingItsReturnedBinding) {
  NativeStateDocument empty;
  ASSERT_EQ(store.load(stateScope, stateSource, empty), NativeStateLoad::Missing);
  empty.local = document.local;
  ASSERT_TRUE(store.save(empty));
  ASSERT_EQ(store.load(stateScope, stateSource, empty), NativeStateLoad::Ready);
  EXPECT_EQ(empty.local.status, "reading");
}
TEST_F(NativeStateStoreTest, CaptureIsImmutableAcrossCurrentStateChanges) {
  auto original = frozen();
  ASSERT_TRUE(capture.freeze(original));
  ASSERT_TRUE(capture.freeze(original));
  auto changed = original;
  changed.local.rating = 5;
  EXPECT_FALSE(capture.freeze(changed));
  NativeStateDocument loaded;
  ASSERT_TRUE(capture.loadCapture(stateScope, stateSource, 900, 1, payload, "native-document", loaded));
  EXPECT_EQ(loaded.local.rating, 4);
  EXPECT_EQ(loaded.payload, payload);
  EXPECT_FALSE(capture.loadCapture(stateScope, stateSource, 901, 1, payload, "native-document", loaded));
  EXPECT_FALSE(capture.loadCapture(stateScope, stateSource, 900, 2, payload, "native-document", loaded));
}
TEST_F(NativeStateStoreTest, ScopeRevisionAndTokenRollbackCannotReplaceAuthority) {
  ASSERT_TRUE(store.save(document));
  const auto before = card.files.at(statePaths.current);
  auto other = document;
  other.scope.account = "account-b";
  EXPECT_FALSE(store.save(other));
  other = document;
  other.source.revision.bytes[0] = 9;
  EXPECT_FALSE(store.save(other));
  other = document;
  --other.token;
  EXPECT_FALSE(store.save(other));
  EXPECT_EQ(card.files.at(statePaths.current), before);
  NativeStateDocument output;
  EXPECT_EQ(store.load({"server-a", "account-b"}, stateSource, output), NativeStateLoad::Conflict);
}
TEST_F(NativeStateStoreTest, BorrowedOldScopeCannotBecomeTheNewFilesScopeDuringValidation) {
  ASSERT_TRUE(store.save(document));
  NativeStateDocument loaded;
  ASSERT_EQ(store.load(stateScope, stateSource, loaded), NativeStateLoad::Ready);
  NativeStateStore foreign({"/foreign", "/foreign.tmp", "/foreign.bak"}, captureScratch.data(), captureScratch.size());
  auto other = document;
  other.scope.account = "account-b";
  ASSERT_TRUE(foreign.save(other));
  const auto bytes = card.files.at("/foreign");
  card.files[statePaths.current] = bytes;
  EXPECT_FALSE(store.save(loaded));
  EXPECT_EQ(card.files.at(statePaths.current), bytes);
}
TEST_F(NativeStateStoreTest, EveryCurrentPublicationCutRestoresCompleteOldOrNewValues) {
  ASSERT_TRUE(store.save(document));
  const auto before = card;
  auto next = document;
  next.token = 6;
  next.local.status = "finished";
  next.local.rating = 5;
  card.events.clear();
  ASSERT_TRUE(store.save(next));
  const auto steps = card.events.size();
  for (bool eager : {false, true})
    for (size_t cut = 0; cut < steps; ++cut) {
      card = before;
      card.events.clear();
      card.eager = eager;
      card.stop = static_cast<int>(cut);
      try {
        store.save(next);
      } catch (const PowerCut&) {
      }
      card.restart();
      NativeStateDocument loaded;
      ASSERT_EQ(store.load(stateScope, stateSource, loaded), NativeStateLoad::Ready) << cut;
      EXPECT_TRUE((loaded.token == 5 && loaded.local.status == "reading" && loaded.local.rating == 4) ||
                  (loaded.token == 6 && loaded.local.status == "finished" && loaded.local.rating == 5))
          << cut;
      EXPECT_EQ(card.handles, 0) << cut;
    }
}
TEST_F(NativeStateStoreTest, EveryCaptureCutRetainsOriginalStateOrNoPublishedCapture) {
  const auto original = frozen();
  const auto before = card;
  card.events.clear();
  ASSERT_TRUE(capture.freeze(original));
  const auto steps = card.events.size();
  for (bool eager : {false, true})
    for (size_t cut = 0; cut < steps; ++cut) {
      card = before;
      card.events.clear();
      card.eager = eager;
      card.stop = static_cast<int>(cut);
      try {
        capture.freeze(original);
      } catch (const PowerCut&) {
      }
      card.restart();
      NativeStateDocument loaded;
      if (capture.loadCapture(stateScope, stateSource, 900, 1, payload, "native-document", loaded)) {
        EXPECT_EQ(loaded.local.status, "reading") << cut;
        EXPECT_EQ(loaded.history.status, "future-status") << cut;
      }
      EXPECT_EQ(card.handles, 0) << cut;
    }
}
TEST_F(NativeStateStoreTest, TruncationAndFutureFormatRetainBytesWithoutReplacement) {
  const auto original = frozen();
  ASSERT_TRUE(capture.freeze(original));
  const auto bytes = card.files.at(stateCapturePaths.current);
  for (size_t n = 0; n < bytes.size(); ++n) {
    card.files[stateCapturePaths.current] = bytes.substr(0, n);
    NativeStateDocument loaded;
    EXPECT_FALSE(capture.loadCapture(stateScope, stateSource, 900, 1, payload, "native-document", loaded)) << n;
    EXPECT_FALSE(capture.freeze(original)) << n;
    EXPECT_EQ(card.files.at(stateCapturePaths.current), bytes.substr(0, n)) << n;
  }
  card.files[stateCapturePaths.current] = bytes;
  card.files[stateCapturePaths.current][3] = '9';
  EXPECT_FALSE(capture.freeze(original));
}
TEST_F(NativeStateStoreTest, FullReviewsAndExplicitEmptyVersusAbsentAreNotTruncated) {
  std::string local(10000, 'a'), known(10000, 'b');
  document.local.reviewNote = local;
  document.history.reviewKnown = document.history.reviewSet = true;
  document.history.reviewNote = known;
  auto original = frozen();
  ASSERT_TRUE(capture.freeze(original));
  NativeStateDocument loaded;
  ASSERT_TRUE(capture.loadCapture(stateScope, stateSource, 900, 1, payload, "native-document", loaded));
  EXPECT_EQ(loaded.local.reviewNote, local);
  EXPECT_EQ(loaded.history.reviewNote, known);
  document.local.reviewNote = {};
  document.local.reviewSet = true;
  document.history.reviewNote = {};
  document.history.reviewSet = false;
  ASSERT_TRUE(store.save(document));
  ASSERT_EQ(store.load(stateScope, stateSource, loaded), NativeStateLoad::Ready);
  EXPECT_TRUE(loaded.local.reviewSet);
  EXPECT_FALSE(loaded.history.reviewSet);
}
