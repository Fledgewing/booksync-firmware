#include <HalStorage.h>
#include <gtest/gtest.h>

#include <fstream>

#include "booksync/reader/Capture.h"
#include "booksync/reader/Locator.h"
using namespace booksync::reader;
using namespace booksync::storage;
namespace {
constexpr PublishPaths paths{"/journal", "/journal.tmp", "/journal.bak"};
}
TEST(ReaderCapture, OpenTurnCloseRebootPreservesRawEvidenceWithoutAcknowledgement) {
  card = {};
  LifecycleJournal journal(paths);
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  Capture capture(journal);
  ASSERT_EQ(capture.open("/Books/fixture.epub", 1), JournalResult::Ok);
  Sample sample{};
  sample.page = 3;
  sample.sourceOffset = 144;
  sample.hasSourceOffset = true;
  sample.sessionSeconds = 21;
  ASSERT_EQ(capture.update(sample, false), JournalResult::Ok);
  ASSERT_EQ(capture.update(sample, true), JournalResult::Ok);
  const auto id = capture.id();
  card.restart();
  LifecycleJournal reboot(paths);
  ASSERT_EQ(reboot.recover(), JournalResult::Ok);
  char bytes[2048];
  JournalEntry entry;
  ASSERT_EQ(reboot.read(id, entry, bytes, sizeof(bytes)), JournalResult::Ok);
  EXPECT_EQ(entry.acknowledged, 0);
  EXPECT_EQ(entry.adapter, "native");
  EXPECT_EQ(entry.owner, "local-unbound");
  EXPECT_NE(entry.payload.find("/Books/fixture.epub"), std::string_view::npos);
  Sample decoded;
  bool closed = false;
  ASSERT_TRUE(Capture::decode(entry.checkpoint, decoded, closed));
  EXPECT_TRUE(closed);
  EXPECT_EQ(decoded.sourceOffset, 144);
  EXPECT_TRUE(decoded.hasSourceOffset);
  EXPECT_EQ(decoded.sessionSeconds, 21);
}
TEST(ReaderCapture, UnchangedSamplesDoNotWriteAndCapacityDoesNotEvict) {
  card = {};
  LifecycleJournal journal(paths, {8192, 1});
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  Capture capture(journal);
  ASSERT_EQ(capture.open("/book", 2), JournalResult::Ok);
  Sample sample{};
  ASSERT_EQ(capture.update(sample, false), JournalResult::Ok);
  card.events.clear();
  ASSERT_EQ(capture.update(sample, false), JournalResult::Ok);
  EXPECT_TRUE(card.events.empty());
  Capture other(journal);
  EXPECT_EQ(other.open("/other", 1), JournalResult::Capacity);
  EXPECT_EQ(journal.pending(), 1);
}
TEST(ReaderCapture, CutEveryCheckpointBoundaryKeepsWholePreviousOrNewSample) {
  card = {};
  LifecycleJournal initial(paths);
  ASSERT_EQ(initial.recover(), JournalResult::Ok);
  Capture capture(initial);
  ASSERT_EQ(capture.open("/book", 1), JournalResult::Ok);
  Sample before{};
  before.sessionSeconds = 10;
  ASSERT_EQ(capture.update(before, false), JournalResult::Ok);
  const auto seed = card.disk;
  Sample after = before;
  after.sessionSeconds = 20;
  card.events.clear();
  ASSERT_EQ(capture.update(after, false), JournalResult::Ok);
  const auto steps = card.events.size();
  for (bool eager : {false, true})
    for (size_t cut = 0; cut < steps; ++cut) {
      card = {};
      card.files = card.disk = seed;
      card.eager = eager;
      LifecycleJournal writer(paths);
      ASSERT_EQ(writer.recover(), JournalResult::Ok);
      Capture resumed(writer, capture.id());
      card.events.clear();
      card.stop = static_cast<int>(cut);
      try {
        resumed.update(after, false);
      } catch (const PowerCut&) {
      }
      card.restart();
      LifecycleJournal reboot(paths);
      ASSERT_EQ(reboot.recover(), JournalResult::Ok) << cut;
      char bytes[2048];
      JournalEntry entry;
      ASSERT_EQ(reboot.read(capture.id(), entry, bytes, sizeof(bytes)), JournalResult::Ok);
      Sample decoded;
      bool closed;
      ASSERT_TRUE(Capture::decode(entry.checkpoint, decoded, closed));
      EXPECT_TRUE(decoded.sessionSeconds == 10 || decoded.sessionSeconds == 20);
      EXPECT_EQ(entry.acknowledged, 0);
    }
}
TEST(ReaderCapture, InvalidInputNeverCreatesPartialIntent) {
  card = {};
  LifecycleJournal journal(paths);
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  Capture capture(journal);
  EXPECT_EQ(capture.open("", 1), JournalResult::Invalid);
  EXPECT_EQ(capture.open(std::string(1025, 'x'), 1), JournalResult::Invalid);
  EXPECT_EQ(journal.pending(), 0);
  Sample value;
  bool closed;
  EXPECT_FALSE(Capture::decode("BSC9", value, closed));
}
TEST(ReaderCapture, MultipartHighlightAndDeletionKeepExactNativeBytesAndSourceUnchanged) {
  card = {};
  const std::string source(4096, 'x');
  card.files["/native"] = card.disk["/native"] = source;
  LifecycleJournal journal(paths);
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  Capture capture(journal);
  ASSERT_EQ(capture.artifact("highlights", "/Books/fixture.epub", "/native"), JournalResult::Ok);
  EXPECT_EQ(journal.pending(), 2);
  EXPECT_EQ(card.files.at("/native"), source);
  uint64_t id = 0, first = 0;
  std::string restored;
  char bytes[4096];
  JournalEntry entry;
  while (journal.next("native", "local-unbound", id, id) == JournalResult::Ok) {
    ASSERT_EQ(journal.read(id, entry, bytes, sizeof(bytes)), JournalResult::Ok);
    ASSERT_EQ(entry.payload.substr(0, 4), "BSA1");
    if (!first)
      first = id;
    else
      EXPECT_EQ(static_cast<uint8_t>(entry.payload[10]), first);
    EXPECT_EQ(entry.payload[5], journal.pending() == id);
    restored.append(entry.payload.substr(32 + 10 + 19));
  }
  EXPECT_EQ(restored, source);
  EXPECT_EQ(capture.artifact("bookmarks", "/Books/fixture.epub", "/missing", true), JournalResult::Ok);
  EXPECT_EQ(journal.pending(), 3);
}
TEST(ReaderCapture, OversizeCapacityReadOnlyAndFullCardNeverChangeNativeSource) {
  card = {};
  const std::string source(4096, 'q');
  card.files["/native"] = card.disk["/native"] = source;
  LifecycleJournal journal(paths, {8192, 1});
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  Capture capture(journal);
  EXPECT_EQ(capture.artifact("highlights", "/book", "/native"), JournalResult::Capacity);
  EXPECT_EQ(card.files.at("/native"), source);
  card.readOnly = true;
  EXPECT_EQ(capture.open("/book", 1), JournalResult::IoError);
  EXPECT_EQ(card.files.at("/native"), source);
  EXPECT_EQ(journal.pending(), 0);
}
TEST(ReaderCapture, NativeCommitKeepsOpeningCounterAndBaselineWithoutAddingTotalsTwice) {
  card = {};
  LifecycleJournal journal(paths);
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  Capture capture(journal);
  ASSERT_EQ(capture.open("/book", 1), JournalResult::Ok);
  Sample sample{};
  sample.hasStats = true;
  sample.totalSeconds = 100;
  sample.counter[0] = 7;
  ASSERT_EQ(capture.update(sample, false), JournalResult::Ok);
  card.events.clear();
  ASSERT_EQ(capture.update(sample, false), JournalResult::Ok);
  EXPECT_TRUE(card.events.empty());
  sample.sessionSeconds = 12;
  ASSERT_EQ(capture.update(sample, false), JournalResult::Ok);
  sample.totalSeconds = 112;
  sample.sessionSeconds = 0;
  ASSERT_EQ(capture.update(sample, true), JournalResult::Ok);
  char bytes[2048];
  JournalEntry entry;
  ASSERT_EQ(journal.read(capture.id(), entry, bytes, sizeof(bytes)), JournalResult::Ok);
  bool closed;
  Sample decoded;
  ASSERT_TRUE(Capture::decode(entry.checkpoint, decoded, closed));
  EXPECT_EQ(decoded.openingTotalSeconds, 100);
  EXPECT_EQ(decoded.openingCounter, sample.counter);
  EXPECT_EQ(decoded.totalSeconds, 112);
  EXPECT_EQ(decoded.sessionSeconds, 0);
}

TEST(ReaderCapture, F03RawSourceLocatorSurvivesJournalWithoutRewritingOrInventingIdentity) {
  card = {};
  LifecycleJournal journal(paths);
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  Capture capture(journal);
  ASSERT_EQ(capture.open("/Books/fixture.epub", NativeEpubProgress), JournalResult::Ok);
  Sample sample;
  std::ifstream input(REPO_ROOT "/test/fixtures/legacy/sd/.crosspoint/epub_305419896/progress.bin", std::ios::binary);
  ASSERT_TRUE(input.read(reinterpret_cast<char*>(sample.rawProgress.data()), 20));
  sample.primaryBytes = 20;
  ASSERT_EQ(capture.update(sample, false), JournalResult::Ok);
  card.restart();
  LifecycleJournal reboot(paths);
  ASSERT_EQ(reboot.recover(), JournalResult::Ok);
  char bytes[2048];
  JournalEntry entry;
  ASSERT_EQ(reboot.read(capture.id(), entry, bytes, sizeof(bytes)), JournalResult::Ok);
  Sample decoded;
  bool closed;
  ASSERT_TRUE(Capture::decode(entry.checkpoint, decoded, closed));
  EXPECT_EQ(decoded.rawProgress, sample.rawProgress);
  constexpr SourceIdentity verified{{{1}}, {{2}}, {{3}}};
  progress_record::Fields anchor;
  EXPECT_EQ(epubAnchor({verified,
                        NativeEpubProgress,
                        1,
                        {reinterpret_cast<const char*>(decoded.rawProgress.data()), decoded.primaryBytes}},
                       verified, anchor),
            ReplayStatus::Ready);
  EXPECT_EQ(anchor.visibleTextOffset, 144);
  EXPECT_EQ(entry.owner, "local-unbound");
}
TEST(ReaderCapture, NativeSessionsKeepStableIdsUtcTimesAndCumulativeCheckpoints) {
  card = {};
  LifecycleJournal journal(paths);
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  Capture capture(journal);
  ASSERT_EQ(capture.open("/book", 1), JournalResult::Ok);
  SessionObservation session;
  session.id.fill('a');
  session.document.fill('b');
  session.startedAt = 1800000000;
  session.endedAt = session.startedAt + 12;
  session.seconds = 12;
  session.utcOffsetMinutes = -60;
  ASSERT_EQ(capture.session(session), JournalResult::Ok);
  card.events.clear();
  ASSERT_EQ(capture.session(session), JournalResult::Ok);
  EXPECT_TRUE(card.events.empty());
  session.seconds = 24;
  session.endedAt += 12;
  ASSERT_EQ(capture.session(session), JournalResult::Ok);
  EXPECT_EQ(journal.pending(), 2);
  session.id.fill('c');
  ASSERT_EQ(capture.session(session), JournalResult::Ok);
  EXPECT_EQ(journal.pending(), 3);
  card.restart();
  LifecycleJournal reboot(paths);
  ASSERT_EQ(reboot.recover(), JournalResult::Ok);
  char buffer[2048];
  JournalEntry entry;
  ASSERT_EQ(reboot.read(2, entry, buffer, sizeof(buffer)), JournalResult::Ok);
  ASSERT_EQ(entry.checkpoint.size(), 90);
  EXPECT_EQ(entry.checkpoint.substr(0, 4), "BSS1");
  EXPECT_EQ(entry.checkpoint.substr(4, 32), std::string(32, 'a'));
  EXPECT_EQ(static_cast<uint8_t>(entry.checkpoint[84]), 24);
  EXPECT_EQ(entry.acknowledged, 0);
}
TEST(ReaderCapture, FullCardDoesNotChangeNativeFilesOrAcknowledgePriorCapture) {
  card = {};
  card.files["/native"] = card.disk["/native"] = "native-source";
  LifecycleJournal journal(paths);
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  Capture capture(journal);
  ASSERT_EQ(capture.open("/book", 1), JournalResult::Ok);
  Sample before;
  before.sessionSeconds = 12;
  ASSERT_EQ(capture.update(before, false), JournalResult::Ok);
  card.full = true;
  Sample after = before;
  after.sessionSeconds = 24;
  EXPECT_EQ(capture.update(after, false), JournalResult::IoError);
  EXPECT_EQ(card.files.at("/native"), "native-source");
  card.restart();
  card.full = false;
  LifecycleJournal reboot(paths);
  ASSERT_EQ(reboot.recover(), JournalResult::Ok);
  char bytes[2048];
  JournalEntry entry;
  ASSERT_EQ(reboot.read(capture.id(), entry, bytes, sizeof(bytes)), JournalResult::Ok);
  Sample recovered;
  bool closed;
  ASSERT_TRUE(Capture::decode(entry.checkpoint, recovered, closed));
  EXPECT_EQ(recovered.sessionSeconds, 12);
  EXPECT_FALSE(closed);
  EXPECT_EQ(entry.acknowledged, 0);
}
