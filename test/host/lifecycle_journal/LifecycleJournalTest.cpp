#include <HalStorage.h>
#include <gtest/gtest.h>

#include "booksync/storage/LifecycleJournal.h"
using namespace booksync::storage;
namespace {
constexpr PublishPaths paths{"/journal", "/journal.tmp", "/journal.bak"};
JournalEntry intent(std::string_view owner = "account-a") {
  return {0, "toy", owner, "document-x", "offline-capture", "pending-remote", 0};
}
void seed() { card = {}; }
void complete(LifecycleJournal& journal, uint64_t id) {
  for (int phase = 0; phase < 5; ++phase)
    ASSERT_EQ(journal.acknowledge(id, static_cast<LifecyclePhase>(phase), "confirmed"), JournalResult::Ok);
}
}  // namespace
TEST(LifecycleJournal, OfflineEnqueueRestartAndScopedFifo) {
  seed();
  LifecycleJournal journal(paths);
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  uint64_t a = 0, b = 0, c = 0, found = 0;
  ASSERT_EQ(journal.enqueue(intent(), a), JournalResult::Ok);
  ASSERT_EQ(journal.enqueue(intent("account-b"), b), JournalResult::Ok);
  ASSERT_EQ(journal.enqueue(intent(), c), JournalResult::Ok);
  card.restart();
  LifecycleJournal reboot(paths);
  ASSERT_EQ(reboot.recover(), JournalResult::Ok);
  ASSERT_EQ(reboot.next("toy", "account-a", 0, found), JournalResult::Ok);
  EXPECT_EQ(found, a);
  ASSERT_EQ(reboot.next("toy", "account-a", a, found), JournalResult::Ok);
  EXPECT_EQ(found, c);
  char buffer[256];
  JournalEntry entry;
  ASSERT_EQ(reboot.read(a, entry, buffer, sizeof(buffer)), JournalResult::Ok);
  EXPECT_EQ(entry.payload, "offline-capture");
  EXPECT_EQ(entry.checkpoint, "pending-remote");
  EXPECT_EQ(entry.acknowledged, 0);
  EXPECT_EQ(reboot.pending(), 3);
}
TEST(LifecycleJournal, DeferredCheckpointIsNotAnAcknowledgementAndFinishRequiresAllPhases) {
  seed();
  LifecycleJournal journal(paths);
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  uint64_t id = 0;
  ASSERT_EQ(journal.enqueue(intent(), id), JournalResult::Ok);
  ASSERT_EQ(journal.checkpoint(id, "remote-outcome-unknown"), JournalResult::Ok);
  card.restart();
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  char buffer[256];
  JournalEntry entry;
  ASSERT_EQ(journal.read(id, entry, buffer, sizeof(buffer)), JournalResult::Ok);
  EXPECT_EQ(entry.acknowledged, 0);
  EXPECT_EQ(entry.checkpoint, "remote-outcome-unknown");
  EXPECT_EQ(journal.finish(id), JournalResult::Incomplete);
  complete(journal, id);
  ASSERT_EQ(journal.finish(id), JournalResult::Ok);
  card.restart();
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  EXPECT_EQ(journal.pending(), 0);
  uint64_t newer = 0;
  ASSERT_EQ(journal.enqueue(intent(), newer), JournalResult::Ok);
  EXPECT_GT(newer, id);
}
TEST(LifecycleJournal, CapacityNeverEvictsAndReservesCheckpointAndAckSpace) {
  seed();
  LifecycleJournal journal(paths, {1400, 1});
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  uint64_t id = 0, rejected = 99;
  ASSERT_EQ(journal.enqueue(intent(), id), JournalResult::Ok);
  EXPECT_EQ(journal.enqueue(intent(), rejected), JournalResult::Capacity);
  EXPECT_EQ(rejected, 99);
  std::string large(LifecycleJournal::MaxCheckpoint, 'x');
  ASSERT_EQ(journal.checkpoint(id, large), JournalResult::Ok);
  EXPECT_EQ(journal.finish(id), JournalResult::Incomplete);
  complete(journal, id);
  ASSERT_EQ(journal.finish(id), JournalResult::Ok);
  ASSERT_EQ(journal.enqueue(intent(), rejected), JournalResult::Ok);
}
TEST(LifecycleJournal, CutsEveryPublicationOperationKeepAcceptedWorkAndAtomicAckCheckpoint) {
  for (bool eager : {false, true}) {
    seed();
    LifecycleJournal initial(paths);
    ASSERT_EQ(initial.recover(), JournalResult::Ok);
    uint64_t id = 0;
    ASSERT_EQ(initial.enqueue(intent(), id), JournalResult::Ok);
    const auto disk = card.disk;
    card.events.clear();
    ASSERT_EQ(initial.acknowledge(id, LifecyclePhase::Match, "confirmed"), JournalResult::Ok);
    const int operations = card.events.size();
    for (int cut = 0; cut < operations; ++cut) {
      SCOPED_TRACE(cut);
      card = {};
      card.disk = disk;
      card.restart();
      card.eager = eager;
      LifecycleJournal journal(paths);
      ASSERT_EQ(journal.recover(), JournalResult::Ok);
      card.events.clear();
      card.stop = cut;
      EXPECT_THROW(journal.acknowledge(id, LifecyclePhase::Match, "confirmed"), PowerCut);
      card.restart();
      LifecycleJournal reboot(paths);
      ASSERT_EQ(reboot.recover(), JournalResult::Ok);
      char buffer[256];
      JournalEntry entry;
      ASSERT_EQ(reboot.read(id, entry, buffer, sizeof(buffer)), JournalResult::Ok);
      EXPECT_EQ(entry.payload, "offline-capture");
      EXPECT_TRUE((entry.acknowledged == 0 && entry.checkpoint == "pending-remote") ||
                  (entry.acknowledged == 1 && entry.checkpoint == "confirmed"));
      ASSERT_EQ(reboot.recover(), JournalResult::Ok);
      EXPECT_EQ(reboot.pending(), 1);
    }
  }
}
TEST(LifecycleJournal, CorruptTailChecksumAndUnknownVersionNeverBecomeEmptySuccess) {
  seed();
  LifecycleJournal journal(paths);
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  uint64_t id = 0;
  ASSERT_EQ(journal.enqueue(intent(), id), JournalResult::Ok);
  const auto valid = card.disk.at(paths.current);
  for (size_t end = 0; end < valid.size(); ++end) {
    card = {};
    card.files[paths.current] = valid.substr(0, end);
    card.disk = card.files;
    LifecycleJournal reboot(paths);
    EXPECT_EQ(reboot.recover(), JournalResult::RecoveryRequired);
    EXPECT_EQ(reboot.enqueue(intent(), id), JournalResult::RecoveryRequired);
    EXPECT_EQ(card.files.at(paths.current), valid.substr(0, end));
  }
  card = {};
  card.files[paths.current] = valid;
  card.files[paths.current].back() ^= 1;
  LifecycleJournal corrupt(paths);
  EXPECT_EQ(corrupt.recover(), JournalResult::RecoveryRequired);
  card.files[paths.backup] = valid;
  card.files[paths.current] = valid;
  card.files[paths.current][4] = 2;
  LifecycleJournal unknown(paths);
  EXPECT_EQ(unknown.recover(), JournalResult::RecoveryRequired);
  EXPECT_EQ(unknown.enqueue(intent(), id), JournalResult::RecoveryRequired);
}
TEST(LifecycleJournal, ReadOnlyAndFullCardPreservePendingAndRecoveryDoesNotWrite) {
  for (bool readOnly : {false, true}) {
    seed();
    LifecycleJournal journal(paths);
    ASSERT_EQ(journal.recover(), JournalResult::Ok);
    uint64_t id = 0;
    ASSERT_EQ(journal.enqueue(intent(), id), JournalResult::Ok);
    card.readOnly = readOnly;
    card.full = !readOnly;
    EXPECT_EQ(journal.checkpoint(id, "unknown"), JournalResult::IoError);
    card.restart();
    card.events.clear();
    ASSERT_EQ(journal.recover(), JournalResult::Ok);
    EXPECT_EQ(journal.pending(), 1);
    for (const auto& event : card.events)
      EXPECT_TRUE(event == "open" || event == "read" || event == "close" || event == "seek");
    EXPECT_EQ(card.handles, 0);
  }
}
TEST(LifecycleJournal, ByteLimitRejectsWithoutChangingAnyAcceptedEntry) {
  seed();
  LifecycleJournal journal(paths, {2000, 8});
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  uint64_t id = 0;
  ASSERT_EQ(journal.enqueue(intent(), id), JournalResult::Ok);
  const auto disk = card.disk;
  EXPECT_EQ(journal.enqueue(intent(), id), JournalResult::Capacity);
  EXPECT_EQ(card.disk, disk);
  EXPECT_EQ(journal.pending(), 1);
}
TEST(LifecycleJournal, InvalidInputsAndSmallReadBuffersCannotMutateJournal) {
  seed();
  LifecycleJournal journal(paths);
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  uint64_t id = 0;
  auto bad = intent();
  bad.owner = {};
  EXPECT_EQ(journal.enqueue(bad, id), JournalResult::Invalid);
  std::string oversized(LifecycleJournal::MaxPayload + 1, 'x');
  bad = intent();
  bad.payload = oversized;
  EXPECT_EQ(journal.enqueue(bad, id), JournalResult::Invalid);
  ASSERT_EQ(journal.enqueue(intent(), id), JournalResult::Ok);
  const auto disk = card.disk;
  EXPECT_EQ(journal.acknowledge(id, static_cast<LifecyclePhase>(5), ""), JournalResult::Invalid);
  EXPECT_EQ(journal.checkpoint(id, oversized), JournalResult::Invalid);
  EXPECT_EQ(journal.checkpoint(id + 1, ""), JournalResult::NotFound);
  JournalEntry out;
  char small[1];
  EXPECT_EQ(journal.read(id, out, small, sizeof(small)), JournalResult::Capacity);
  EXPECT_EQ(card.disk, disk);
  EXPECT_EQ(card.handles, 0);
}
TEST(LifecycleJournal, DuplicateAckDoesNotClearEarlierPhases) {
  seed();
  LifecycleJournal journal(paths);
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  auto entry = intent();
  entry.acknowledged = 15;
  uint64_t id = 0;
  ASSERT_EQ(journal.enqueue(entry, id), JournalResult::Ok);
  ASSERT_EQ(journal.acknowledge(id, LifecyclePhase::Progress, "one"), JournalResult::Ok);
  ASSERT_EQ(journal.acknowledge(id, LifecyclePhase::Progress, "two"), JournalResult::Ok);
  card.restart();
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  char buffer[256];
  ASSERT_EQ(journal.read(id, entry, buffer, sizeof(buffer)), JournalResult::Ok);
  EXPECT_EQ(entry.acknowledged, 31);
  EXPECT_EQ(entry.checkpoint, "two");
}
TEST(LifecycleJournal, CutsEnqueueAndFinishPreserveEveryOtherUnacknowledgedEntry) {
  for (bool finishing : {false, true})
    for (bool eager : {false, true}) {
      seed();
      LifecycleJournal initial(paths);
      ASSERT_EQ(initial.recover(), JournalResult::Ok);
      uint64_t a = 0, b = 0;
      ASSERT_EQ(initial.enqueue(intent(), a), JournalResult::Ok);
      ASSERT_EQ(initial.enqueue(intent(), b), JournalResult::Ok);
      if (finishing) complete(initial, a);
      const auto disk = card.disk;
      card.events.clear();
      ASSERT_EQ(finishing ? initial.finish(a) : initial.enqueue(intent(), b), JournalResult::Ok);
      const auto operations = card.events.size();
      for (size_t cut = 0; cut < operations; ++cut) {
        SCOPED_TRACE(cut);
        card = {};
        card.disk = disk;
        card.restart();
        card.eager = eager;
        LifecycleJournal journal(paths);
        ASSERT_EQ(journal.recover(), JournalResult::Ok);
        card.events.clear();
        card.stop = cut;
        uint64_t newId = 0;
        EXPECT_THROW(finishing ? journal.finish(a) : journal.enqueue(intent(), newId), PowerCut);
        card.restart();
        LifecycleJournal reboot(paths);
        ASSERT_EQ(reboot.recover(), JournalResult::Ok);
        JournalEntry entry;
        char buffer[256];
        ASSERT_EQ(reboot.read(2, entry, buffer, sizeof(buffer)), JournalResult::Ok);
        EXPECT_EQ(entry.payload, "offline-capture");
        EXPECT_EQ(entry.acknowledged, 0);
        if (!finishing) EXPECT_EQ(reboot.read(a, entry, buffer, sizeof(buffer)), JournalResult::Ok);
        EXPECT_EQ(card.handles, 0);
      }
    }
}
TEST(LifecycleJournal, ShortStagingWritesNeverReplaceAcceptedWork) {
  seed();
  LifecycleJournal initial(paths);
  ASSERT_EQ(initial.recover(), JournalResult::Ok);
  uint64_t id = 0;
  ASSERT_EQ(initial.enqueue(intent(), id), JournalResult::Ok);
  const auto disk = card.disk;
  // Cut within the envelope, record header, copied payload, checkpoint, padding and CRC.
  card.writes = 0;
  ASSERT_EQ(initial.checkpoint(id, "unknown"), JournalResult::Ok);
  const auto writes = card.writes;
  for (int write = 0; write < writes; ++write)
    for (int prefix : {0, 1, 3}) {
      card = {};
      card.disk = disk;
      card.restart();
      card.eager = true;
      LifecycleJournal journal(paths);
      ASSERT_EQ(journal.recover(), JournalResult::Ok);
      card.writes = 0;
      card.cutWrite = write;
      card.prefix = prefix;
      EXPECT_THROW(journal.checkpoint(id, "unknown"), PowerCut);
      card.restart();
      ASSERT_EQ(journal.recover(), JournalResult::Ok);
      char buffer[256];
      JournalEntry entry;
      ASSERT_EQ(journal.read(id, entry, buffer, sizeof(buffer)), JournalResult::Ok);
      EXPECT_EQ(entry.checkpoint, "pending-remote");
      EXPECT_EQ(entry.acknowledged, 0);
    }
}
TEST(LifecycleJournal, FailingAnyStorageOperationRequiresReconciliationAndNeverDropsAcceptedWork) {
  seed();
  LifecycleJournal initial(paths);
  ASSERT_EQ(initial.recover(), JournalResult::Ok);
  uint64_t id = 0;
  ASSERT_EQ(initial.enqueue(intent(), id), JournalResult::Ok);
  const auto disk = card.disk;
  card.events.clear();
  ASSERT_EQ(initial.checkpoint(id, "unknown"), JournalResult::Ok);
  const auto operations = card.events.size();
  for (size_t failure = 0; failure < operations; ++failure) {
    SCOPED_TRACE(failure);
    card = {};
    card.disk = disk;
    card.restart();
    LifecycleJournal journal(paths);
    ASSERT_EQ(journal.recover(), JournalResult::Ok);
    card.events.clear();
    card.fail = failure;
    const auto result = journal.checkpoint(id, "unknown");
    EXPECT_TRUE(result == JournalResult::Ok || result == JournalResult::IoError);
    EXPECT_EQ(card.handles, 0);
    card.restart();
    ASSERT_EQ(journal.recover(), JournalResult::Ok);
    EXPECT_EQ(journal.pending(), 1);
    char buffer[256];
    JournalEntry entry;
    ASSERT_EQ(journal.read(id, entry, buffer, sizeof(buffer)), JournalResult::Ok);
    EXPECT_EQ(entry.payload, "offline-capture");
  }
}
TEST(LifecycleJournal, BackupRecoveryIgnoresStagingAndUnsupportedBackupBlocksOlderWriter) {
  seed();
  LifecycleJournal journal(paths);
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  uint64_t id = 0;
  ASSERT_EQ(journal.enqueue(intent(), id), JournalResult::Ok);
  const auto valid = card.files.at(paths.current);
  card = {};
  card.files[paths.backup] = valid;
  card.files[paths.staging] = "torn";
  card.disk = card.files;
  EXPECT_EQ(journal.recover(), JournalResult::Ok);
  EXPECT_EQ(journal.pending(), 1);
  ASSERT_EQ(journal.checkpoint(id, "restored"), JournalResult::Ok);
  card.files[paths.backup][4] = 2;
  EXPECT_EQ(journal.recover(), JournalResult::RecoveryRequired);
}

TEST(LifecycleJournal, EmptyAndUnsupportedLimitsAreExplicit) {
  seed();
  LifecycleJournal journal(paths);
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  EXPECT_EQ(journal.finish(1), JournalResult::NotFound);
  EXPECT_EQ(journal.checkpoint(1, ""), JournalResult::NotFound);
  for (JournalLimits limits :
       {JournalLimits{27, 1}, JournalLimits{262145, 1}, JournalLimits{2000, 65}, JournalLimits{2000, 0}}) {
    LifecycleJournal invalid(paths, limits);
    EXPECT_EQ(invalid.recover(), JournalResult::Invalid);
  }
}
TEST(LifecycleJournal, FirstEnqueueCutsRecoverAbsentOrCompleteWithoutReplayingStaging) {
  seed();
  LifecycleJournal initial(paths);
  ASSERT_EQ(initial.recover(), JournalResult::Ok);
  uint64_t id = 0;
  card.events.clear();
  ASSERT_EQ(initial.enqueue(intent(), id), JournalResult::Ok);
  const auto operations = card.events.size();
  for (bool eager : {false, true})
    for (size_t cut = 0; cut < operations; ++cut) {
      seed();
      card.eager = eager;
      LifecycleJournal journal(paths);
      ASSERT_EQ(journal.recover(), JournalResult::Ok);
      card.events.clear();
      card.stop = cut;
      EXPECT_THROW(journal.enqueue(intent(), id), PowerCut);
      card.restart();
      ASSERT_EQ(journal.recover(), JournalResult::Ok);
      EXPECT_LE(journal.pending(), 1);
      if (journal.pending()) {
        char buffer[256];
        JournalEntry entry;
        ASSERT_EQ(journal.read(1, entry, buffer, sizeof(buffer)), JournalResult::Ok);
        EXPECT_EQ(entry.payload, "offline-capture");
        EXPECT_EQ(entry.acknowledged, 0);
      }
    }
}
TEST(LifecycleJournal, MaximumOpaquePayloadAndCheckpointRoundTripWithoutInterpretation) {
  seed();
  LifecycleJournal journal(paths);
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  std::string payload(LifecycleJournal::MaxPayload, '\0'), checkpoint(LifecycleJournal::MaxCheckpoint, '\xff');
  for (size_t i = 0; i < payload.size(); ++i) payload[i] = char(i);
  auto entry = intent();
  entry.payload = payload;
  entry.checkpoint = checkpoint;
  uint64_t id = 0;
  ASSERT_EQ(journal.enqueue(entry, id), JournalResult::Ok);
  card.restart();
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  std::string buffer(6000, '\0');
  ASSERT_EQ(journal.read(id, entry, buffer.data(), buffer.size()), JournalResult::Ok);
  EXPECT_EQ(entry.payload, payload);
  EXPECT_EQ(entry.checkpoint, checkpoint);
}

TEST(LifecycleJournal, MediaDamageAfterRecoveryCannotBeResealedOrOverwritePendingWork) {
  seed();
  LifecycleJournal journal(paths);
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  uint64_t id = 0;
  ASSERT_EQ(journal.enqueue(intent(), id), JournalResult::Ok);
  ASSERT_EQ(journal.checkpoint(id, "uncertain"), JournalResult::Ok);
  card.files[paths.current].back() ^= 1;
  const auto damaged = card.files[paths.current];
  EXPECT_EQ(journal.checkpoint(id, "new"), JournalResult::IoError);
  EXPECT_EQ(journal.checkpoint(id, "new"), JournalResult::RecoveryRequired);
  EXPECT_EQ(card.files[paths.current], damaged);
}
