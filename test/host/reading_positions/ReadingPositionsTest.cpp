// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include <fstream>
#include <iterator>

#include "booksync/import/ReadingPositions.h"
using namespace booksync;
using namespace booksync::storage;
using namespace booksync::importer;
namespace {
constexpr PublishPaths paths{"/import", "/import.tmp", "/import.bak"};
constexpr SourceIdentity source{{{1}}, {{2}}, {{3}}};
std::string fixture(const char* path) {
  std::ifstream in(std::string(REPO_ROOT) + "/test/fixtures/legacy/" + path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), {}};
}
const char* primary = "sd/.crosspoint/epub_305419896/progress.bin";
const char* backup = "sd/.crosspoint/epub_305419896/progress.bin.bak";
}  // namespace
TEST(ReadingPositions, AllF03PositionSourcesRetainedExactlyAcrossTwoBoots) {
  card = {};
  const std::pair<const char*, PositionFormat> cases[]{
      {primary, PositionFormat::Epub},
      {backup, PositionFormat::Epub},
      {"sd/.crosspoint/book_0123456789abcdef0123456789abcdef/progress.bin", PositionFormat::Epub},
      {"sd/.crosspoint/book_0123456789abcdef0123456789abcdef/progress.bin.bak", PositionFormat::Epub},
      {"variants/progress/legacy-4.bin", PositionFormat::Epub},
      {"variants/progress/legacy-6.bin", PositionFormat::Epub},
      {"variants/progress/legacy-10.bin", PositionFormat::Epub},
      {"sd/.crosspoint/epub_305419896/progress-path.bin", PositionFormat::Path},
      {"sd/.crosspoint/epub_305419896/links.bin", PositionFormat::Links},
      {"sd/.crosspoint/book_0123456789abcdef0123456789abcdef/bookorbit_sync.bin", PositionFormat::Settled},
      {"sd/.crosspoint/orbismart/0123456789abcdef0123456789abcdef.0", PositionFormat::Intent},
      {"variants/progress/torn.bin", PositionFormat::Epub},
      {"variants/unknown/future.bin", PositionFormat::Epub}};
  for (auto [path, format] : cases) card.files[path] = fixture(path);
  card.disk = card.files;
  for (int boot = 0; boot < 2; ++boot) {
    LifecycleJournal journal(paths);
    ASSERT_EQ(journal.recover(), JournalResult::Ok);
    auto worker = std::make_unique<ReadingPositions>(journal);
    for (auto [path, format] : cases) {
      PositionStatus status;
      uint64_t id = 0;
      ASSERT_EQ(worker->retain(path, format, source, status, id), JournalResult::Ok) << path;
      EXPECT_EQ(status, std::string_view(path).find("torn.bin") != std::string_view::npos ? PositionStatus::Corrupt
                        : std::string_view(path).find("future.bin") != std::string_view::npos
                            ? PositionStatus::Unsupported
                            : PositionStatus::Supported);
      std::array<char, 4992> bytes;
      JournalEntry entry;
      ASSERT_EQ(journal.read(id, entry, bytes.data(), bytes.size()), JournalResult::Ok);
      PositionEvidence evidence;
      ASSERT_TRUE(ReadingPositions::decode(entry.payload, evidence));
      EXPECT_EQ(evidence.path, path);
      EXPECT_EQ(evidence.raw, fixture(path));
      EXPECT_EQ(evidence.source, source);
      EXPECT_EQ(entry.acknowledged, 0);
      EXPECT_TRUE(entry.checkpoint.empty());
      EXPECT_EQ(card.files[path], fixture(path));
    }
    EXPECT_EQ(journal.pending(), std::size(cases));
    card.restart();
  }
}
TEST(ReadingPositions, NativeSelectionPreservesRawBackupAndCaptureAnchor) {
  auto a = fixture(primary), b = fixture(backup);
  PositionEvidence p{PositionFormat::Epub, PositionStatus::Supported, source, primary, a, 0};
  PositionEvidence q{PositionFormat::Epub, PositionStatus::Supported, source, backup, b, 0};
  reader::LocatorView locator;
  reader::Sample sample;
  ASSERT_TRUE(ReadingPositions::select(&p, &q, locator, sample));
  EXPECT_EQ(locator.raw, a);
  EXPECT_EQ(sample.sourceOffset, 144);
  EXPECT_EQ(sample.primaryBytes, 20);
  EXPECT_EQ(sample.backupBytes, 20);
  EXPECT_FALSE(sample.hasStats);
  progress_record::Fields anchor;
  EXPECT_EQ(reader::epubAnchor(locator, source, anchor), reader::ReplayStatus::Ready);
  p.raw = "broken";
  ASSERT_TRUE(ReadingPositions::select(&p, &q, locator, sample));
  EXPECT_EQ(sample.sourceOffset, 96);
  q.path = "another-cache/progress.bin.bak";
  EXPECT_FALSE(ReadingPositions::select(&p, &q, locator, sample));
  q.path = backup;
  q.source.edition.bytes[0] = 4;
  EXPECT_FALSE(ReadingPositions::select(&p, &q, locator, sample));
  q.source = {};
  p.source = {};
  EXPECT_FALSE(ReadingPositions::select(&p, &q, locator, sample));
}
TEST(ReadingPositions, FullReadOnlyAndOversizeLeaveOriginalAndNoSuccessProof) {
  for (bool full : {false, true}) {
    card = {};
    card.files["/source"] = fixture(primary);
    card.disk = card.files;
    LifecycleJournal journal(paths);
    ASSERT_EQ(journal.recover(), JournalResult::Ok);
    auto worker = std::make_unique<ReadingPositions>(journal);
    card.full = full;
    card.readOnly = !full;
    PositionStatus status;
    uint64_t id = 0;
    EXPECT_NE(worker->retain("/source", PositionFormat::Epub, source, status, id), JournalResult::Ok);
    EXPECT_EQ(card.files["/source"], fixture(primary));
  }
  card = {};
  card.files["/source"] = std::string(2049, 'x');
  LifecycleJournal journal(paths);
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  auto worker = std::make_unique<ReadingPositions>(journal);
  PositionStatus status;
  uint64_t id = 0;
  EXPECT_EQ(worker->retain("/source", PositionFormat::Epub, source, status, id), JournalResult::Capacity);
  EXPECT_EQ(journal.pending(), 0);
}
TEST(ReadingPositions, SourceChangeDoesNotReuseProofAndNoWriteOnDuplicate) {
  card = {};
  card.files["/source"] = fixture(primary);
  card.disk = card.files;
  LifecycleJournal journal(paths);
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  auto worker = std::make_unique<ReadingPositions>(journal);
  PositionStatus status;
  uint64_t id = 0, again = 0;
  ASSERT_EQ(worker->retain("/source", PositionFormat::Epub, source, status, id), JournalResult::Ok);
  card.events.clear();
  ASSERT_EQ(worker->retain("/source", PositionFormat::Epub, source, status, again), JournalResult::Ok);
  EXPECT_EQ(id, again);
  for (const auto& event : card.events) EXPECT_EQ(event.find("write"), std::string::npos);
  card.files["/source"] = fixture(backup);
  ASSERT_EQ(worker->retain("/source", PositionFormat::Epub, source, status, again), JournalResult::Ok);
  EXPECT_NE(id, again);
}
TEST(ReadingPositions, CutEveryPublicationBoundaryRetriesWithoutDuplicateOrSourceLoss) {
  card = {};
  card.files["/source"] = fixture(primary);
  card.disk = card.files;
  const auto seed = card.disk;
  LifecycleJournal journal(paths);
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  auto worker = std::make_unique<ReadingPositions>(journal);
  PositionStatus status;
  uint64_t id = 0;
  card.events.clear();
  ASSERT_EQ(worker->retain("/source", PositionFormat::Epub, source, status, id), JournalResult::Ok);
  const auto steps = card.events.size();
  for (bool eager : {false, true})
    for (size_t cut = 0; cut < steps; ++cut) {
      card = {};
      card.files = card.disk = seed;
      card.eager = eager;
      LifecycleJournal before(paths);
      ASSERT_EQ(before.recover(), JournalResult::Ok);
      auto first = std::make_unique<ReadingPositions>(before);
      card.events.clear();
      card.stop = int(cut);
      try {
        first->retain("/source", PositionFormat::Epub, source, status, id);
      } catch (PowerCut&) {
      }
      card.restart();
      LifecycleJournal after(paths);
      ASSERT_EQ(after.recover(), JournalResult::Ok);
      auto retry = std::make_unique<ReadingPositions>(after);
      ASSERT_EQ(retry->retain("/source", PositionFormat::Epub, source, status, id), JournalResult::Ok);
      EXPECT_EQ(after.pending(), 1) << cut;
      EXPECT_EQ(card.files["/source"], fixture(primary));
    }
}
TEST(ReadingPositions, UnsupportedAndCorruptAreDistinctAndNeverReplayed) {
  EXPECT_EQ(ReadingPositions::inspect(PositionFormat::Epub, fixture("variants/progress/torn.bin")),
            PositionStatus::Corrupt);
  EXPECT_EQ(ReadingPositions::inspect(PositionFormat::Epub, fixture("variants/unknown/future.bin")),
            PositionStatus::Unsupported);
  for (auto format : {PositionFormat::Path, PositionFormat::Settled, PositionFormat::Intent}) {
    EXPECT_EQ(ReadingPositions::inspect(format, "FUT9"), PositionStatus::Unsupported);
  }
  auto intent = fixture("sd/.crosspoint/orbismart/0123456789abcdef0123456789abcdef.0");
  ASSERT_EQ(ReadingPositions::inspect(PositionFormat::Intent, intent), PositionStatus::Supported);
  intent.back() ^= 1;
  EXPECT_EQ(ReadingPositions::inspect(PositionFormat::Intent, intent), PositionStatus::Corrupt);
  auto p = fixture(primary);
  p[11] = 4;
  EXPECT_EQ(ReadingPositions::inspect(PositionFormat::Epub, p), PositionStatus::Corrupt);
}
TEST(ReadingPositions, UnknownDestinationBlocksImportAndLeavesCallerOutputs) {
  card = {};
  card.files["/source"] = fixture(primary);
  card.files["/import"] = "future-journal";
  card.disk = card.files;
  LifecycleJournal journal(paths);
  ASSERT_NE(journal.recover(), JournalResult::Ok);
  auto worker = std::make_unique<ReadingPositions>(journal);
  PositionStatus status = PositionStatus::Unsupported;
  uint64_t id = 99;
  EXPECT_NE(worker->retain("/source", PositionFormat::Epub, source, status, id), JournalResult::Ok);
  EXPECT_EQ(id, 99);
  EXPECT_EQ(status, PositionStatus::Unsupported);
  EXPECT_EQ(card.files["/source"], fixture(primary));
  EXPECT_EQ(card.files["/import"], "future-journal");
}
TEST(ReadingPositions, DecodeRejectsEnvelopeDamageAndPreservesOutput) {
  card = {};
  card.files["/source"] = fixture(primary);
  LifecycleJournal journal(paths);
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  auto worker = std::make_unique<ReadingPositions>(journal);
  PositionStatus status;
  uint64_t id = 0;
  ASSERT_EQ(worker->retain("/source", PositionFormat::Epub, source, status, id), JournalResult::Ok);
  std::array<char, 4992> buffer;
  JournalEntry entry;
  ASSERT_EQ(journal.read(id, entry, buffer.data(), buffer.size()), JournalResult::Ok);
  std::string envelope(entry.payload);
  PositionEvidence untouched{};
  untouched.path = "sentinel";
  for (size_t i : {size_t(0), size_t(5), size_t(6), size_t(10), size_t(12), envelope.size() - 1}) {
    auto broken = envelope;
    broken[i] ^= 127;
    EXPECT_FALSE(ReadingPositions::decode(broken, untouched)) << i;
    EXPECT_EQ(untouched.path, "sentinel");
  }
  EXPECT_FALSE(ReadingPositions::decode(std::string_view(envelope).substr(0, 79), untouched));
}
