#include <HalStorage.h>
#include <gtest/gtest.h>

#include "booksync/reader/Capture.h"

using namespace std::string_view_literals;
using namespace booksync::reader;
using namespace booksync::storage;
namespace {
constexpr PublishPaths paths{"/bound", "/bound.tmp", "/bound.bak"};
SourceIdentity source() {
  SourceIdentity value{};
  value.book.bytes[0] = 1;
  value.edition.bytes[0] = 2;
  value.revision.bytes[0] = 3;
  return value;
}
}  // namespace
TEST(BoundCapture, AccountSourceAndOpaqueProofSurviveRestart) {
  card = {};
  LifecycleJournal journal(paths);
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  Capture capture(journal);
  ASSERT_EQ(capture.open("/book.epub", 1, {source(), {"server-a", "account-a"}, "opaque\0proof"sv}), JournalResult::Ok);
  Sample sample{};
  sample.pageBegin = 4;
  sample.pageEnd = 12;
  sample.observedAt = 200;
  ASSERT_EQ(capture.update(sample, true), JournalResult::Ok);
  card.restart();
  LifecycleJournal restored(paths);
  ASSERT_EQ(restored.recover(), JournalResult::Ok);
  char bytes[4096];
  JournalEntry entry;
  ASSERT_EQ(restored.read(capture.id(), entry, bytes, sizeof(bytes)), JournalResult::Ok);
  OpenCapture opened;
  ASSERT_TRUE(Capture::decodeOpen(entry.payload, opened));
  EXPECT_EQ(opened.path, "/book.epub");
  EXPECT_EQ(opened.binding.scope.account, "account-a");
  EXPECT_EQ(opened.binding.source, source());
  EXPECT_EQ(opened.binding.proof, "opaque\0proof"sv);
  bool closed = false;
  ASSERT_TRUE(Capture::decode(entry.checkpoint, sample, closed));
  EXPECT_TRUE(closed);
  EXPECT_EQ(sample.pageBegin, 4);
  EXPECT_EQ(sample.pageEnd, 12);
  EXPECT_EQ(sample.observedAt, 200);
  EXPECT_EQ(entry.acknowledged, 0);
}
TEST(BoundCapture, ClosedBoundSnapshotCannotChangeOrReopen) {
  card = {};
  LifecycleJournal journal(paths);
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  Capture capture(journal);
  ASSERT_EQ(capture.open("/book", 1, {source(), {"server", "account"}, "proof"}), JournalResult::Ok);
  Sample sample{};
  sample.observedAt = 200;
  ASSERT_EQ(capture.update(sample, true), JournalResult::Ok);
  sample.pageEnd = 1;
  EXPECT_EQ(capture.update(sample, true), JournalResult::Invalid);
  EXPECT_EQ(capture.update(sample, false), JournalResult::Invalid);
}
TEST(BoundCapture, LegacyObservationRemainsUnbound) {
  card = {};
  LifecycleJournal journal(paths);
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  Capture capture(journal);
  ASSERT_EQ(capture.open("/book", 1), JournalResult::Ok);
  char bytes[4096];
  JournalEntry entry;
  ASSERT_EQ(journal.read(capture.id(), entry, bytes, sizeof(bytes)), JournalResult::Ok);
  OpenCapture opened;
  ASSERT_TRUE(Capture::decodeOpen(entry.payload, opened));
  EXPECT_FALSE(opened.bound);
  EXPECT_EQ(opened.path, "/book");
  EXPECT_TRUE(opened.binding.scope.account.empty());
}
TEST(BoundCapture, InvalidBindingDoesNotSilentlyBecomeBoundOrWrite) {
  card = {};
  LifecycleJournal journal(paths);
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  Capture capture(journal);
  EXPECT_EQ(capture.open("/book", 1, {{}, {"server", "account"}, "proof"}), JournalResult::Invalid);
  EXPECT_EQ(journal.pending(), 0);
  EXPECT_EQ(capture.open("/book", 1, {source(), {"server", ""}, "proof"}), JournalResult::Invalid);
  EXPECT_EQ(journal.pending(), 0);
}
TEST(BoundCapture, TruncatedOrUnknownRecordsNeverProduceBindingEvidence) {
  card = {};
  LifecycleJournal journal(paths);
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  Capture capture(journal);
  ASSERT_EQ(capture.open("/book", 1, {source(), {"server", "account"}, "proof"}), JournalResult::Ok);
  char bytes[4096];
  JournalEntry entry;
  ASSERT_EQ(journal.read(capture.id(), entry, bytes, sizeof(bytes)), JournalResult::Ok);
  OpenCapture output;
  for (size_t length = 0; length < entry.payload.size(); ++length)
    EXPECT_FALSE(Capture::decodeOpen(entry.payload.substr(0, length), output)) << length;
  std::string unknown(entry.payload);
  unknown[3] = '3';
  EXPECT_FALSE(Capture::decodeOpen(unknown, output));
}
TEST(BoundCapture, EveryCloseCutRetainsWholeSourceBoundCheckpoint) {
  card = {};
  LifecycleJournal initial(paths);
  ASSERT_EQ(initial.recover(), JournalResult::Ok);
  Capture capture(initial);
  ASSERT_EQ(capture.open("/book", 1, {source(), {"server", "account"}, "proof"}), JournalResult::Ok);
  Sample sample{};
  sample.observedAt = 100;
  ASSERT_EQ(capture.update(sample, false), JournalResult::Ok);
  const auto seed = card.disk;
  sample.pageEnd = 3;
  sample.observedAt = 200;
  card.events.clear();
  ASSERT_EQ(capture.update(sample, true), JournalResult::Ok);
  const auto steps = card.events.size();
  for (bool eager : {false, true})
    for (size_t cut = 0; cut < steps; ++cut) {
      card = {};
      card.files = card.disk = seed;
      card.eager = eager;
      LifecycleJournal writer(paths);
      ASSERT_EQ(writer.recover(), JournalResult::Ok);
      // A resumed object is deliberately not permitted to invent source binding.
      // Cut the journal checkpoint itself using the exact newly encoded bytes.
      char bytes[4096];
      JournalEntry entry;
      ASSERT_EQ(writer.read(capture.id(), entry, bytes, sizeof(bytes)), JournalResult::Ok);
      std::string next(entry.checkpoint);
      next[4] = 1;
      next[120] = 3;
      next[124] = char(200);
      card.events.clear();
      card.stop = int(cut);
      try {
        writer.checkpoint(capture.id(), next);
      } catch (const PowerCut&) {
      }
      card.restart();
      LifecycleJournal restored(paths);
      ASSERT_EQ(restored.recover(), JournalResult::Ok);
      ASSERT_EQ(restored.read(capture.id(), entry, bytes, sizeof(bytes)), JournalResult::Ok);
      OpenCapture opened;
      ASSERT_TRUE(Capture::decodeOpen(entry.payload, opened));
      EXPECT_EQ(opened.binding.source, source());
      bool closed = false;
      ASSERT_TRUE(Capture::decode(entry.checkpoint, sample, closed));
      EXPECT_EQ(sample.observedAt, closed ? 200 : 100);
      EXPECT_EQ(sample.pageEnd, closed ? 3 : 0);
      EXPECT_EQ(entry.acknowledged, 0);
    }
}
TEST(BoundCapture, UnchangedObservationNeedsNoNewWriteOrNativeIdFromCaller) {
  card = {};
  LifecycleJournal journal(paths);
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  Capture capture(journal);
  ASSERT_EQ(capture.open("/book", 1, {source(), {"server", "account"}, "proof"}), JournalResult::Ok);
  Sample sample{};
  sample.observedAt = 100;
  ASSERT_EQ(capture.update(sample, false), JournalResult::Ok);
  card.events.clear();
  EXPECT_TRUE(capture.unchanged(sample, false));
  EXPECT_EQ(capture.update(sample, false), JournalResult::Ok);
  EXPECT_TRUE(card.events.empty());
}
