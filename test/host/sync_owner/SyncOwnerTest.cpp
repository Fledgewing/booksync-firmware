#include <HalStorage.h>
#include <gtest/gtest.h>

#include <array>

#include "booksync/services/SyncOwner.h"
using namespace booksync;
using namespace booksync::storage;
namespace {
constexpr PublishPaths paths{"/sync", "/sync.tmp", "/sync.bak"};
class Toy final : public ServerAdapter {
 public:
  std::array<unsigned, 5> commits{};
  unsigned calls = 0, reconciles = 0;
  bool uncertain = true;
  SyncOwner* owner = nullptr;
  Capabilities capabilities() const override { return {}; }
  Snapshot cachedSnapshot() const override { return {}; }
  CommandStatus deliver(const SyncWriterToken&, Intent) override { return CommandStatus::Unavailable; }
  PhaseOutcome syncPhase(const SyncWriterToken&, const PhaseRequest& request) override {
    ++calls;
    EXPECT_EQ(request.entry.owner, "scope:fern");
    EXPECT_EQ(request.entry.document, "leaf:violet");
    EXPECT_EQ(request.entry.checkpoint, "native-capture");
    reconciles += request.reconcile;
    if (owner) {
      EXPECT_EQ(owner->step(), SyncResult::Busy);
      EXPECT_EQ(owner->deactivate(), SyncResult::Busy);
    }
    auto& n = commits[unsigned(request.phase)];
    if (!n) ++n;  // Toy remote deduplicates by scoped entry ID + phase.
    return uncertain ? PhaseOutcome::Uncertain : PhaseOutcome::Confirmed;
  }
};
struct SyncOwnerTest : testing::Test {
  LifecycleJournal journal{paths};
  Toy toy;
  std::array<char, SyncOwner::ScratchBytes> scratch{};
  void SetUp() override {
    card = {};
    ASSERT_EQ(journal.recover(), JournalResult::Ok);
    uint64_t id = 0;
    ASSERT_EQ(journal.enqueue({0, "toy:grove", "scope:fern", "leaf:violet", "offline", "native-capture", 0}, id),
              JournalResult::Ok);
  }
};
}  // namespace
TEST_F(SyncOwnerTest, RejectsSecondOwnerAcrossDifferentJournalsAndRetainsQueue) {
  SyncOwner first(journal, toy, "toy:grove", "scope:fern", scratch.data(), scratch.size());
  LifecycleJournal other(paths);
  SyncOwner second(other, toy, "toy:grove", "scope:fern", scratch.data(), scratch.size());
  EXPECT_EQ(first.activate(), SyncResult::Ready);
  EXPECT_EQ(first.activate(), SyncResult::Busy);
  EXPECT_EQ(second.activate(), SyncResult::Busy);
  EXPECT_EQ(second.step(), SyncResult::Inactive);
  EXPECT_EQ(first.deactivate(), SyncResult::Ready);
  EXPECT_EQ(second.activate(), SyncResult::Ready);
  EXPECT_EQ(other.pending(), 1);
  EXPECT_EQ(toy.calls, 0);
}
TEST_F(SyncOwnerTest, UncertainAckRetriesSamePhaseAfterTransitionWithoutLosingCapture) {
  {
    SyncOwner first(journal, toy, "toy:grove", "scope:fern", scratch.data(), scratch.size());
    ASSERT_EQ(first.activate(), SyncResult::Ready);
    EXPECT_EQ(first.step(), SyncResult::Uncertain);
    EXPECT_EQ(journal.pending(), 1);
  }
  card.restart();
  SyncOwner next(journal, toy, "toy:grove", "scope:fern", scratch.data(), scratch.size());
  ASSERT_EQ(next.activate(), SyncResult::Ready);
  toy.uncertain = false;
  toy.owner = &next;
  for (int i = 0; i < 5; ++i) EXPECT_EQ(next.step(), SyncResult::Advanced);
  EXPECT_EQ(toy.reconciles, 1);
  for (auto n : toy.commits) EXPECT_EQ(n, 1);
  EXPECT_EQ(next.step(), SyncResult::Completed);
  EXPECT_EQ(next.step(), SyncResult::Idle);
  EXPECT_EQ(journal.pending(), 0);
}
TEST_F(SyncOwnerTest, DifferentAccountCannotDrainOldOfflineQueue) {
  SyncOwner owner(journal, toy, "toy:grove", "scope:other", scratch.data(), scratch.size());
  ASSERT_EQ(owner.activate(), SyncResult::Ready);
  EXPECT_EQ(owner.step(), SyncResult::Idle);
  EXPECT_EQ(journal.pending(), 1);
  EXPECT_EQ(toy.calls, 0);
}
TEST_F(SyncOwnerTest, FailedAttemptPublicationSendsNothingAndBlocksUntilRecovery) {
  SyncOwner owner(journal, toy, "toy:grove", "scope:fern", scratch.data(), scratch.size());
  ASSERT_EQ(owner.activate(), SyncResult::Ready);
  card.readOnly = true;
  EXPECT_EQ(owner.step(), SyncResult::StorageError);
  EXPECT_EQ(owner.step(), SyncResult::StorageError);
  EXPECT_EQ(toy.calls, 0);
}
TEST_F(SyncOwnerTest, RejectsSmallScratchAndInvalidScopeWithoutTakingLease) {
  SyncOwner invalid(journal, toy, "", "scope:fern", scratch.data(), scratch.size());
  EXPECT_EQ(invalid.activate(), SyncResult::Invalid);
  SyncOwner small(journal, toy, "toy", "scope", scratch.data(), 1);
  EXPECT_EQ(small.activate(), SyncResult::Invalid);
  SyncOwner valid(journal, toy, "toy", "scope", scratch.data(), scratch.size());
  EXPECT_EQ(valid.activate(), SyncResult::Ready);
}
TEST_F(SyncOwnerTest, CutsEveryStorageOperationAcrossAllFivePhasesPreserveOfflineWork) {
  const auto seedDisk = card.disk;
  for (unsigned phase = 0; phase < 5; ++phase) {
    card.disk = seedDisk;
    card.restart();
    toy = {};
    toy.uncertain = false;
    std::map<std::string, std::string> before;
    size_t operations = 0;
    {
      SyncOwner owner(journal, toy, "toy:grove", "scope:fern", scratch.data(), scratch.size());
      ASSERT_EQ(owner.activate(), SyncResult::Ready);
      for (unsigned i = 0; i < phase; ++i) ASSERT_EQ(owner.step(), SyncResult::Advanced);
      before = card.disk;
      card.events.clear();
      ASSERT_EQ(owner.step(), SyncResult::Advanced);
      operations = card.events.size();
    }
    for (bool eager : {false, true})
      for (size_t cut = 0; cut < operations; ++cut) {
        SCOPED_TRACE(phase);
        SCOPED_TRACE(cut);
        SCOPED_TRACE(eager);
        card.disk = before;
        card.restart();
        card.eager = eager;
        toy = {};
        toy.uncertain = false;
        {
          SyncOwner owner(journal, toy, "toy:grove", "scope:fern", scratch.data(), scratch.size());
          ASSERT_EQ(owner.activate(), SyncResult::Ready);
          card.events.clear();
          card.stop = cut;
          EXPECT_THROW(owner.step(), PowerCut);
        }
        card.restart();
        SyncOwner reboot(journal, toy, "toy:grove", "scope:fern", scratch.data(), scratch.size());
        ASSERT_EQ(reboot.activate(), SyncResult::Ready);
        ASSERT_EQ(journal.pending(), 1);
        for (unsigned i = phase; i < 5; ++i) {
          const auto r = reboot.step();
          ASSERT_TRUE(r == SyncResult::Advanced || r == SyncResult::Completed);
        }
        if (journal.pending()) EXPECT_EQ(reboot.step(), SyncResult::Completed);
        EXPECT_EQ(journal.pending(), 0);
        for (auto n : toy.commits) EXPECT_LE(n, 1);
      }
  }
}
TEST_F(SyncOwnerTest, FailedActivationReleasesLeaseAndMalformedPhaseStateNeverSends) {
  card.files[paths.current].back() ^= 1;
  SyncOwner broken(journal, toy, "toy:grove", "scope:fern", scratch.data(), scratch.size());
  EXPECT_EQ(broken.activate(), SyncResult::StorageError);
  card.restart();
  SyncOwner owner(journal, toy, "toy:grove", "scope:fern", scratch.data(), scratch.size());
  ASSERT_EQ(owner.activate(), SyncResult::Ready);
  ASSERT_EQ(journal.attempt(1, LifecyclePhase::State), JournalResult::Ok);
  EXPECT_EQ(owner.step(), SyncResult::StorageError);
  EXPECT_EQ(toy.calls, 0);
  EXPECT_EQ(journal.pending(), 1);
}
TEST_F(SyncOwnerTest, UnsupportedAdapterKeepsAttemptAndPendingWork) {
  class Unsupported final : public ServerAdapter {
   public:
    Capabilities capabilities() const override { return {}; }
    Snapshot cachedSnapshot() const override { return {}; }
    CommandStatus deliver(const SyncWriterToken&, Intent) override { return CommandStatus::Unavailable; }
  } unsupported;
  SyncOwner owner(journal, unsupported, "toy:grove", "scope:fern", scratch.data(), scratch.size());
  ASSERT_EQ(owner.activate(), SyncResult::Ready);
  EXPECT_EQ(owner.step(), SyncResult::Unavailable);
  EXPECT_EQ(owner.step(), SyncResult::Unavailable);
  EXPECT_EQ(journal.pending(), 1);
  JournalEntry entry;
  ASSERT_EQ(journal.read(1, entry, scratch.data(), scratch.size()), JournalResult::Ok);
  EXPECT_EQ(entry.attempting, 1);
  EXPECT_EQ(entry.acknowledged, 0);
  EXPECT_EQ(entry.checkpoint, "native-capture");
}
TEST_F(SyncOwnerTest, DamagedUncertainRecordCannotReachAdapterOrBeResealed) {
  SyncOwner owner(journal, toy, "toy:grove", "scope:fern", scratch.data(), scratch.size());
  ASSERT_EQ(owner.activate(), SyncResult::Ready);
  ASSERT_EQ(owner.step(), SyncResult::Uncertain);
  ASSERT_EQ(toy.calls, 1);
  card.files[paths.current].back() ^= 1;
  const auto damaged = card.files.at(paths.current);
  EXPECT_EQ(owner.step(), SyncResult::StorageError);
  EXPECT_EQ(toy.calls, 1);
  EXPECT_EQ(card.files.at(paths.current), damaged);
}
