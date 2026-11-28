#include <HalStorage.h>
#include <gtest/gtest.h>

#include <array>

#include "booksync/services/Finish.h"
#include "booksync/services/SyncOwner.h"
using namespace booksync;
using namespace booksync::storage;
namespace {
class Toy : public ServerAdapter {
 public:
  unsigned positions = 0, writes = 0, reconciles = 0, normalStates = 0, effects = 0;
  std::array<std::array<bool, 5>, 65> committed{};
  FinishValues lastDesired{};
  bool uncertain = false, uncertainPosition = false;
  Capabilities capabilities() const override { return {}; }
  Snapshot cachedSnapshot() const override { return {}; }
  CommandStatus deliver(const SyncWriterToken&, Intent) override { return CommandStatus::Unavailable; }
  PhaseOutcome syncPhase(const SyncWriterToken&, const PhaseRequest& r) override {
    if (r.phase == LifecyclePhase::Progress) {
      ++positions;
      if (uncertainPosition) return PhaseOutcome::Uncertain;
    }
    if (r.phase == LifecyclePhase::State) ++normalStates;
    return PhaseOutcome::Confirmed;
  }
  PhaseOutcome finishPhase(const SyncWriterToken&, const FinishRequest& r) override {
    EXPECT_GT(positions, 0U);
    EXPECT_EQ(r.entry.checkpoint, "resolved-identity");
    const auto phase = unsigned(r.phase);
    EXPECT_GE(phase, 1U);
    EXPECT_LT(phase, 5U);
    EXPECT_LT(r.entry.id, committed.size());
    if (phase == 2 && !r.intent.day.empty()) EXPECT_TRUE(committed[r.entry.id][1]);
    if (phase == 3 && r.intent.desired.status != ReadingStatus::Unknown) EXPECT_TRUE(committed[r.entry.id][2]);
    if (phase == 4 && r.intent.desired.ratingKnown) EXPECT_TRUE(committed[r.entry.id][3]);
    if (!committed[r.entry.id][phase]) {
      committed[r.entry.id][phase] = true;
      ++effects;
    }
    lastDesired = r.intent.desired;
    ++writes;
    reconciles += r.reconcile;
    return uncertain ? PhaseOutcome::Uncertain : PhaseOutcome::Confirmed;
  }
};
class Proof : public FinishPositionProof {
 public:
  bool outgoing = true;
  bool delivered(const JournalEntry&) const override { return outgoing; }
};
struct FinishTest : testing::Test {
  Proof proof;
  LifecycleJournal journal{{"/finish", "/finish.tmp", "/finish.bak"}};
  std::array<char, SyncOwner::ScratchBytes> scratch{}, finishScratch{};
  Finish finish{journal, proof, finishScratch.data(), finishScratch.size()};
  Toy toy;
  uint64_t position = 0, intent = 0;
  FinishValues before{ReadingStatus::Reading, true, 2}, after{ReadingStatus::Finished, true, 4};
  void SetUp() override {
    card = {};
    ASSERT_EQ(journal.recover(), JournalResult::Ok);
    ASSERT_EQ(journal.enqueue({0, "toy:grove", "scope:fern", "leaf:violet", "capture", "resolved-identity"}, position),
              JournalResult::Ok);
  }
  void enqueue() {
    ASSERT_EQ(finish.enqueue(position, "toy:grove", "scope:fern", "leaf:violet", before, after, "2026-11-27", intent),
              JournalResult::Ok);
  }
  void drain(SyncOwner& owner) {
    for (unsigned i = 0; i < 30; ++i) {
      const auto r = owner.step();
      if (r == SyncResult::Idle) return;
      ASSERT_TRUE(r == SyncResult::Advanced || r == SyncResult::Completed);
    }
    FAIL() << "did not settle";
  }
};
}  // namespace
TEST_F(FinishTest, OfflineRestartDeliversPositionThenStateStatusRatingAndRetainsUndoReceipt) {
  enqueue();
  card.restart();
  SyncOwner owner(journal, toy, "toy:grove", "scope:fern", scratch.data(), scratch.size(), nullptr, nullptr, &finish);
  ASSERT_EQ(owner.activate(), SyncResult::Ready);
  drain(owner);
  EXPECT_EQ(toy.positions, 1U);
  EXPECT_EQ(toy.normalStates, 0U);
  EXPECT_EQ(toy.writes, 4U);
  EXPECT_EQ(toy.lastDesired.status, ReadingStatus::Finished);
  EXPECT_EQ(toy.lastDesired.rating, 4);
  EXPECT_EQ(journal.pending(), 1);
  uint64_t undo = 0;
  EXPECT_EQ(finish.undo(intent, "toy:grove", "scope:fern", undo), JournalResult::Ok);
  drain(owner);
  EXPECT_EQ(toy.writes, 7U);
  EXPECT_EQ(toy.lastDesired.status, ReadingStatus::Reading);
  EXPECT_EQ(toy.lastDesired.rating, 2);
  uint64_t repeat = 0;
  EXPECT_EQ(finish.undo(intent, "toy:grove", "scope:fern", repeat), JournalResult::Ok);
  EXPECT_EQ(repeat, undo);
  drain(owner);
  EXPECT_EQ(toy.writes, 7U);
}
TEST_F(FinishTest, UndoBeforeAnyAttemptCancelsWithoutRemoteFinish) {
  enqueue();
  uint64_t undo = 0;
  ASSERT_EQ(finish.undo(intent, "toy:grove", "scope:fern", undo), JournalResult::Ok);
  card.restart();
  SyncOwner owner(journal, toy, "toy:grove", "scope:fern", scratch.data(), scratch.size(), nullptr, nullptr, &finish);
  ASSERT_EQ(owner.activate(), SyncResult::Ready);
  drain(owner);
  EXPECT_EQ(toy.writes, 0U);
}
TEST_F(FinishTest, UnknownWriteRetainedAndUndoWaitsForReconciliation) {
  enqueue();
  {
    SyncOwner owner(journal, toy, "toy:grove", "scope:fern", scratch.data(), scratch.size(), nullptr, nullptr, &finish);
    ASSERT_EQ(owner.activate(), SyncResult::Ready);
    for (int i = 0; i < 6; ++i) ASSERT_TRUE(owner.step() == (i == 5 ? SyncResult::Completed : SyncResult::Advanced));
    toy.uncertain = true;
    EXPECT_EQ(owner.step(), SyncResult::Uncertain);
    uint64_t undo = 0;
    EXPECT_EQ(finish.undo(intent, "toy:grove", "scope:fern", undo), JournalResult::Ok);
  }
  card.restart();
  toy.uncertain = false;
  SyncOwner owner(journal, toy, "toy:grove", "scope:fern", scratch.data(), scratch.size(), nullptr, nullptr, &finish);
  ASSERT_EQ(owner.activate(), SyncResult::Ready);
  drain(owner);
  EXPECT_EQ(toy.reconciles, 1U);
  EXPECT_EQ(toy.writes, 8U);
  EXPECT_EQ(toy.effects, 7U);
}
TEST_F(FinishTest, ScopeMismatchMissingPositionAndReadOnlyNeverAcceptAnIntent) {
  EXPECT_EQ(finish.enqueue(position, "toy:grove", "scope:other", "leaf:violet", before, after, "", intent),
            JournalResult::Invalid);
  EXPECT_EQ(finish.enqueue(999, "toy:grove", "scope:fern", "leaf:violet", before, after, "", intent),
            JournalResult::NotFound);
  card.readOnly = true;
  EXPECT_NE(finish.enqueue(position, "toy:grove", "scope:fern", "leaf:violet", before, after, "", intent),
            JournalResult::Ok);
  EXPECT_EQ(toy.writes, 0U);
}
TEST_F(FinishTest, MissingCoordinatorCannotRetirePositionBarrier) {
  enqueue();
  SyncOwner owner(journal, toy, "toy:grove", "scope:fern", scratch.data(), scratch.size());
  ASSERT_EQ(owner.activate(), SyncResult::Ready);
  EXPECT_EQ(owner.step(), SyncResult::Unavailable);
  EXPECT_EQ(toy.positions, 0U);
  EXPECT_EQ(journal.pending(), 2);
}
TEST_F(FinishTest, WrongAccountCannotUndoOrDrainAndUnknownPriorValueIsNotInvented) {
  before = {};
  enqueue();
  uint64_t undo = 0;
  EXPECT_EQ(finish.undo(intent, "toy:grove", "scope:other", undo), JournalResult::Invalid);
  {
    SyncOwner other(journal, toy, "toy:grove", "scope:other", scratch.data(), scratch.size(), nullptr, nullptr,
                    &finish);
    ASSERT_EQ(other.activate(), SyncResult::Ready);
    EXPECT_EQ(other.step(), SyncResult::Idle);
  }
  SyncOwner owner(journal, toy, "toy:grove", "scope:fern", scratch.data(), scratch.size(), nullptr, nullptr, &finish);
  ASSERT_EQ(owner.activate(), SyncResult::Ready);
  drain(owner);
  EXPECT_EQ(finish.undo(intent, "toy:grove", "scope:fern", undo), JournalResult::Invalid);
}
TEST_F(FinishTest, NewerChoicePreventsOldUndo) {
  enqueue();
  uint64_t second = 0, undo = 0;
  ASSERT_EQ(finish.enqueue(position, "toy:grove", "scope:fern", "leaf:violet", after, before, "", second),
            JournalResult::Ok);
  EXPECT_EQ(finish.undo(intent, "toy:grove", "scope:fern", undo), JournalResult::Invalid);
  SyncOwner owner(journal, toy, "toy:grove", "scope:fern", scratch.data(), scratch.size(), nullptr, nullptr, &finish);
  ASSERT_EQ(owner.activate(), SyncResult::Ready);
  drain(owner);
  EXPECT_EQ(toy.writes, 3U);
  EXPECT_EQ(finish.undo(intent, "toy:grove", "scope:fern", undo), JournalResult::Invalid);
}
TEST_F(FinishTest, MalformedOrFutureIntentNeverReachesAdapter) {
  const std::string future = "BSFI\2future";
  uint64_t id = 0;
  ASSERT_EQ(journal.enqueue({0, "toy:grove", "scope:fern", "leaf:violet", future, {}, 1}, id), JournalResult::Ok);
  SyncOwner owner(journal, toy, "toy:grove", "scope:fern", scratch.data(), scratch.size(), nullptr, nullptr, &finish);
  ASSERT_EQ(owner.activate(), SyncResult::Ready);
  for (int i = 0; i < 3; ++i) ASSERT_EQ(owner.step(), SyncResult::Advanced);
  EXPECT_EQ(owner.step(), SyncResult::StorageError);
  EXPECT_EQ(toy.writes, 0U);
}
TEST_F(FinishTest, FullJournalKeepsExistingCaptureAndDeclinesNewFinish) {
  LifecycleJournal tiny{{"/finish", "/finish.tmp", "/finish.bak"}, {256 * 1024, 1}};
  ASSERT_EQ(tiny.recover(), JournalResult::Ok);
  Finish bounded(tiny, proof, finishScratch.data(), finishScratch.size());
  EXPECT_EQ(bounded.enqueue(position, "toy:grove", "scope:fern", "leaf:violet", before, after, "", intent),
            JournalResult::Capacity);
  EXPECT_EQ(tiny.pending(), 1);
  EXPECT_EQ(toy.writes, 0U);
}
TEST_F(FinishTest, EveryPublicationCutDuringEnqueueCancelBarrierAttemptAckAndCompensationRecovers) {
  const auto seed = card.disk;
  size_t powerCuts = 0;
  // Enumerate actual storage operations, both buffered and eager media. These
  // branches cover enqueue, cancellation, position barrier and remote attempt/ack.
  for (unsigned operation = 0; operation < 8; ++operation) {
    card.disk = seed;
    card.restart();
    ASSERT_EQ(journal.recover(), JournalResult::Ok);
    if (operation) enqueue();
    if (operation >= 2) {
      SyncOwner owner(journal, toy, "toy:grove", "scope:fern", scratch.data(), scratch.size(), nullptr, nullptr,
                      &finish);
      ASSERT_EQ(owner.activate(), SyncResult::Ready);
      for (int i = 0; i < (operation == 2 ? 5 : 6); ++i)
        ASSERT_TRUE(owner.step() == (i == 5 ? SyncResult::Completed : SyncResult::Advanced));
      if (operation >= 4) {
        const unsigned completed = operation == 7 ? 1 : operation - 3;
        for (unsigned i = 0; i < completed; ++i) ASSERT_EQ(owner.step(), SyncResult::Advanced);
      }
    }
    const auto disk = card.disk;
    auto action = [&] {
      uint64_t id = 0;
      if (!operation)
        return finish.enqueue(position, "toy:grove", "scope:fern", "leaf:violet", before, after, "", id) ==
               JournalResult::Ok;
      if (operation == 1 || operation == 7)
        return finish.undo(intent, "toy:grove", "scope:fern", id) == JournalResult::Ok;
      SyncOwner owner(journal, toy, "toy:grove", "scope:fern", scratch.data(), scratch.size(), nullptr, nullptr,
                      &finish);
      if (owner.activate() != SyncResult::Ready) return false;
      const auto result = owner.step();
      return result == SyncResult::Advanced || result == SyncResult::Completed;
    };
    card.events.clear();
    ASSERT_TRUE(action());
    const auto n = card.events.size();
    for (bool eager : {false, true})
      for (size_t cut = 0; cut < n; ++cut) {
        SCOPED_TRACE(operation);
        SCOPED_TRACE(cut);
        SCOPED_TRACE(eager);
        ++powerCuts;
        card.disk = disk;
        card.restart();
        ASSERT_EQ(journal.recover(), JournalResult::Ok);
        card.eager = eager;
        card.events.clear();
        card.stop = cut;
        EXPECT_THROW(action(), PowerCut);
        card.restart();
        ASSERT_EQ(journal.recover(), JournalResult::Ok);
        // Recovery never infers confirmation from a failed local publication.
        SyncOwner reboot(journal, toy, "toy:grove", "scope:fern", scratch.data(), scratch.size(), nullptr, nullptr,
                         &finish);
        ASSERT_EQ(reboot.activate(), SyncResult::Ready);
        drain(reboot);
      }
  }
  RecordProperty("power_cuts", powerCuts);
}
TEST_F(FinishTest, ConfirmedFetchCannotSatisfyOutgoingPositionBarrier) {
  enqueue();
  proof.outgoing = false;
  SyncOwner owner(journal, toy, "toy:grove", "scope:fern", scratch.data(), scratch.size(), nullptr, nullptr, &finish);
  ASSERT_EQ(owner.activate(), SyncResult::Ready);
  for (int i = 0; i < 5; ++i) ASSERT_EQ(owner.step(), SyncResult::Advanced);
  EXPECT_EQ(owner.step(), SyncResult::StorageError);
  EXPECT_EQ(toy.writes, 0U);
  EXPECT_EQ(journal.pending(), 2);
}
TEST_F(FinishTest, CannotAttachWrongJournalOrOverlappingWorkspace) {
  Finish overlapping(journal, proof, scratch.data(), scratch.size());
  SyncOwner owner(journal, toy, "toy:grove", "scope:fern", scratch.data(), scratch.size(), nullptr, nullptr,
                  &overlapping);
  EXPECT_EQ(owner.activate(), SyncResult::Invalid);
  LifecycleJournal other{{"/other", "/other.tmp", "/other.bak"}};
  Finish wrong(other, proof, finishScratch.data(), finishScratch.size());
  SyncOwner wrongOwner(journal, toy, "toy:grove", "scope:fern", scratch.data(), scratch.size(), nullptr, nullptr,
                       &wrong);
  EXPECT_EQ(wrongOwner.activate(), SyncResult::Invalid);
}
TEST_F(FinishTest, UndoDuringUnknownFinalPositionWaitsForItsProofAndResolvedIdentity) {
  enqueue();
  {
    SyncOwner owner(journal, toy, "toy:grove", "scope:fern", scratch.data(), scratch.size(), nullptr, nullptr, &finish);
    ASSERT_EQ(owner.activate(), SyncResult::Ready);
    for (int i = 0; i < 4; ++i) ASSERT_EQ(owner.step(), SyncResult::Advanced);
    toy.uncertainPosition = true;
    ASSERT_EQ(owner.step(), SyncResult::Uncertain);
    uint64_t undo = 0;
    ASSERT_EQ(finish.undo(intent, "toy:grove", "scope:fern", undo), JournalResult::Ok);
    EXPECT_EQ(toy.writes, 0U);
  }
  card.restart();
  toy.uncertainPosition = false;
  SyncOwner owner(journal, toy, "toy:grove", "scope:fern", scratch.data(), scratch.size(), nullptr, nullptr, &finish);
  ASSERT_EQ(owner.activate(), SyncResult::Ready);
  drain(owner);
  EXPECT_EQ(toy.writes, 7U);
}
TEST_F(FinishTest, RepeatedUnknownResultNeverBecomesAnAcknowledgment) {
  enqueue();
  SyncOwner owner(journal, toy, "toy:grove", "scope:fern", scratch.data(), scratch.size(), nullptr, nullptr, &finish);
  ASSERT_EQ(owner.activate(), SyncResult::Ready);
  for (int i = 0; i < 6; ++i) ASSERT_EQ(owner.step(), i == 5 ? SyncResult::Completed : SyncResult::Advanced);
  toy.uncertain = true;
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(owner.step(), SyncResult::Uncertain);
    JournalEntry saved;
    ASSERT_EQ(journal.read(intent, saved, finishScratch.data(), finishScratch.size()), JournalResult::Ok);
    EXPECT_EQ(saved.acknowledged, 1);
    EXPECT_EQ(saved.attempting, 2);
  }
  EXPECT_EQ(toy.effects, 1U);
  EXPECT_EQ(toy.reconciles, 2U);
}
TEST_F(FinishTest, RejectsIntentOnceOrdinaryStateExchangeHasStarted) {
  ASSERT_EQ(journal.attempt(position, LifecyclePhase::State), JournalResult::Ok);
  EXPECT_EQ(finish.enqueue(position, "toy:grove", "scope:fern", "leaf:violet", before, after, "", intent),
            JournalResult::Invalid);
}
TEST_F(FinishTest, UndoRestoresOnlyFieldsOwnedByTheIntentAndPreservesExplicitRatingClear) {
  after = {ReadingStatus::Unknown, true, 0};
  enqueue();
  SyncOwner owner(journal, toy, "toy:grove", "scope:fern", scratch.data(), scratch.size(), nullptr, nullptr, &finish);
  ASSERT_EQ(owner.activate(), SyncResult::Ready);
  drain(owner);
  uint64_t undo = 0;
  ASSERT_EQ(finish.undo(intent, "toy:grove", "scope:fern", undo), JournalResult::Ok);
  JournalEntry saved;
  FinishIntent decoded;
  ASSERT_EQ(journal.read(undo, saved, finishScratch.data(), finishScratch.size()), JournalResult::Ok);
  ASSERT_TRUE(Finish::decode(saved.payload, decoded));
  EXPECT_EQ(decoded.before.status, ReadingStatus::Unknown);
  EXPECT_TRUE(decoded.before.ratingKnown);
  EXPECT_EQ(decoded.before.rating, 0);
  EXPECT_EQ(decoded.desired.status, ReadingStatus::Unknown);
  EXPECT_TRUE(decoded.desired.ratingKnown);
  EXPECT_EQ(decoded.desired.rating, 2);
  drain(owner);
}
TEST_F(FinishTest, GuardResultIsDurableBeforeAcknowledgmentAndSurvivesRestartUntilApply) {
  class Checkpoints : public Toy {
   public:
    unsigned guarded = 0;
    bool applied = false;
    PhaseOutcome finishPhase(const SyncWriterToken&, const FinishRequest& r) override {
      EXPECT_NE(r.confirmedCheckpoint, nullptr);
      if (r.phase == LifecyclePhase::Stats && r.entry.checkpoint == "resolved-identity") {
        EXPECT_FALSE(r.reconcile);
        ++guarded;
        *r.confirmedCheckpoint = "resolved-identity|guard-result";
        return PhaseOutcome::Checkpointed;
      }
      EXPECT_EQ(r.entry.checkpoint, "resolved-identity|guard-result");
      if (r.phase == LifecyclePhase::Stats) EXPECT_TRUE(r.reconcile);
      if (r.phase == LifecyclePhase::Progress) applied = true;
      return PhaseOutcome::Confirmed;
    }
  } adapter;
  enqueue();
  {
    SyncOwner owner(journal, adapter, "toy:grove", "scope:fern", scratch.data(), scratch.size(), nullptr, nullptr,
                    &finish);
    ASSERT_EQ(owner.activate(), SyncResult::Ready);
    for (int i = 0; i < 6; ++i) ASSERT_EQ(owner.step(), i == 5 ? SyncResult::Completed : SyncResult::Advanced);
    ASSERT_EQ(owner.step(), SyncResult::Advanced);
    JournalEntry saved;
    ASSERT_EQ(journal.read(intent, saved, finishScratch.data(), finishScratch.size()), JournalResult::Ok);
    EXPECT_EQ(saved.acknowledged, 1);
    EXPECT_EQ(saved.attempting, 2);
    EXPECT_EQ(saved.checkpoint, "resolved-identity|guard-result");
    EXPECT_FALSE(adapter.applied);
  }
  card.restart();
  SyncOwner owner(journal, adapter, "toy:grove", "scope:fern", scratch.data(), scratch.size(), nullptr, nullptr,
                  &finish);
  ASSERT_EQ(owner.activate(), SyncResult::Ready);
  drain(owner);
  EXPECT_EQ(adapter.guarded, 1U);
  EXPECT_TRUE(adapter.applied);
}
TEST_F(FinishTest, QueuedUndoInheritsTheReconciledResultWithoutOverwritingItsOwnCheckpoint) {
  class Results : public Toy {
   public:
    unsigned restored = 0;
    PhaseOutcome finishPhase(const SyncWriterToken&, const FinishRequest& r) override {
      if (!r.intent.undoOf) {
        if (r.phase == LifecyclePhase::Stats) {
          if (!r.reconcile) return PhaseOutcome::Uncertain;
          *r.confirmedCheckpoint = "reconciled-original";
        } else if (r.phase == LifecyclePhase::Annotations) {
          EXPECT_EQ(r.entry.checkpoint, "reconciled-original");
          *r.confirmedCheckpoint = "settled-original";
        } else {
          EXPECT_EQ(r.entry.checkpoint, "settled-original");
        }
      } else if (r.phase == LifecyclePhase::Annotations) {
        EXPECT_EQ(r.entry.checkpoint, "settled-original");
        *r.confirmedCheckpoint = "undo-result";
        ++restored;
      } else {
        EXPECT_EQ(r.entry.checkpoint, "undo-result");
      }
      return PhaseOutcome::Confirmed;
    }
  } adapter;
  enqueue();
  {
    SyncOwner owner(journal, adapter, "toy:grove", "scope:fern", scratch.data(), scratch.size(), nullptr, nullptr,
                    &finish);
    ASSERT_EQ(owner.activate(), SyncResult::Ready);
    for (int i = 0; i < 6; ++i) ASSERT_EQ(owner.step(), i == 5 ? SyncResult::Completed : SyncResult::Advanced);
    ASSERT_EQ(owner.step(), SyncResult::Uncertain);
    uint64_t undo = 0;
    ASSERT_EQ(finish.undo(intent, "toy:grove", "scope:fern", undo), JournalResult::Ok);
  }
  card.restart();
  SyncOwner owner(journal, adapter, "toy:grove", "scope:fern", scratch.data(), scratch.size(), nullptr, nullptr,
                  &finish);
  ASSERT_EQ(owner.activate(), SyncResult::Ready);
  drain(owner);
  EXPECT_EQ(adapter.restored, 1U);
}
