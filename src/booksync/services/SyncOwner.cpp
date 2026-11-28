// SPDX-License-Identifier: GPL-3.0-or-later
#include "SyncOwner.h"

#include <Logging.h>

#include "Finish.h"
namespace booksync {
namespace {
struct Unlock {
  std::atomic_flag& flag;
  ~Unlock() { flag.clear(); }
};
SyncResult storageError() {
  LOG_ERR("BSS", "Sync blocked by journal failure; recover before retry");
  return SyncResult::StorageError;
}
}  // namespace
std::atomic<SyncOwner*> SyncOwner::active_{nullptr};
SyncOwner::SyncOwner(storage::LifecycleJournal& journal, ServerAdapter& adapter, std::string_view adapterId,
                     std::string_view account, char* scratch, size_t capacity, SyncObserver observer, void* context,
                     Finish* finish)
    : journal_(journal),
      adapter_(adapter),
      adapterId_(adapterId),
      account_(account),
      scratch_(scratch),
      capacity_(capacity),
      observer_(observer),
      context_(context),
      finish_(finish) {}
void SyncOwner::observe(SyncCheckpoint point, uint64_t id, unsigned phase) const {
  if (observer_) observer_(point, id, phase, context_);
}
SyncOwner::~SyncOwner() { deactivate(); }
SyncResult SyncOwner::activate() {
  if (busy_.test_and_set()) return SyncResult::Busy;
  Unlock unlock{busy_};
  if (adapterId_.empty() || account_.empty() || adapterId_.size() > storage::LifecycleJournal::MaxIdentity ||
      account_.size() > storage::LifecycleJournal::MaxIdentity || !scratch_ || capacity_ < ScratchBytes)
    return SyncResult::Invalid;
  if (finish_ &&
      (&finish_->journal_ != &journal_ || !finish_->ready() ||
       (reinterpret_cast<uintptr_t>(scratch_) < reinterpret_cast<uintptr_t>(finish_->scratch_) + finish_->capacity_ &&
        reinterpret_cast<uintptr_t>(finish_->scratch_) < reinterpret_cast<uintptr_t>(scratch_) + capacity_)))
    return SyncResult::Invalid;
  SyncOwner* expected = nullptr;
  if (!active_.compare_exchange_strong(expected, this)) return SyncResult::Busy;
  if (journal_.recover() != storage::JournalResult::Ok) {
    active_.store(nullptr);
    return storageError();
  }
  observe(SyncCheckpoint::Activated);
  return SyncResult::Ready;
}
SyncResult SyncOwner::deactivate() {
  if (busy_.test_and_set()) return SyncResult::Busy;
  Unlock unlock{busy_};
  SyncOwner* expected = this;
  return active_.compare_exchange_strong(expected, nullptr) ? SyncResult::Ready : SyncResult::Inactive;
}
SyncResult SyncOwner::step() {
  if (busy_.test_and_set()) return SyncResult::Busy;
  Unlock unlock{busy_};
  if (active_.load() != this) return SyncResult::Inactive;
  using namespace storage;
  if (journal_.verify() != JournalResult::Ok) return storageError();
  if (!finish_) {
    uint64_t cursor = 0;
    JournalResult scan;
    while ((scan = journal_.next(adapterId_, account_, cursor, cursor)) == JournalResult::Ok) {
      JournalEntry candidate;
      if (journal_.read(cursor, candidate, scratch_, capacity_) != JournalResult::Ok) return storageError();
      if (Finish::recognizes(candidate.payload) && !Finish::settled(candidate)) return SyncResult::Unavailable;
    }
    if (scan != JournalResult::NotFound) return storageError();
  }
  uint64_t id = 0;
  JournalEntry entry;
  auto result = JournalResult::Ok;
  do {
    result = journal_.next(adapterId_, account_, id, id);
    if (result != JournalResult::Ok) break;
    if (journal_.read(id, entry, scratch_, capacity_) != JournalResult::Ok) return storageError();
  } while (Finish::recognizes(entry.payload) && Finish::settled(entry));
  if (result == JournalResult::NotFound) return SyncResult::Idle;
  if (result != JournalResult::Ok) return storageError();
  if (Finish::recognizes(entry.payload)) {
    if (!finish_) return SyncResult::Unavailable;
    SyncWriterToken token;
    return finish_->step(token, adapter_, entry);
  }
  if (entry.acknowledged == LifecycleJournal::AllPhases) {
    if (entry.attempting) return storageError();
    if (finish_ && finish_->positionDelivered(entry) != JournalResult::Ok) return storageError();
    if (journal_.finish(id) != JournalResult::Ok) return storageError();
    observe(SyncCheckpoint::Finished, id);
    return SyncResult::Completed;
  }
  unsigned phase = 0;
  while (entry.acknowledged & (1U << phase)) ++phase;
  // Reject impossible/out-of-order state rather than acknowledge a different phase.
  if (entry.acknowledged != (1U << phase) - 1 || (entry.attempting && entry.attempting != phase + 1))
    return storageError();
  const auto selected = static_cast<LifecyclePhase>(phase);
  if ((selected == LifecyclePhase::State || selected == LifecyclePhase::Progress) && finish_) {
    bool suppressState = false, cancelPosition = false;
    if (finish_->positionPlan(entry, suppressState, cancelPosition) != JournalResult::Ok) return storageError();
    if ((selected == LifecyclePhase::State && suppressState) ||
        (selected == LifecyclePhase::Progress && cancelPosition)) {
      // F20 owns status/rating for this final capture. Do not send them through
      // ordinary state exchange before the required final position delivery.
      if (entry.attempting || journal_.acknowledge(id, selected, entry.checkpoint) != JournalResult::Ok)
        return storageError();
      observe(SyncCheckpoint::AckStored, id, phase);
      return SyncResult::Advanced;
    }
  }
  const bool reconcile = entry.attempting != 0;
  if (!reconcile && journal_.attempt(id, selected) != JournalResult::Ok) return storageError();
  observe(SyncCheckpoint::AttemptStored, id, phase);
  // Only this serial call can borrow a token; it never survives owner transition.
  SyncWriterToken token;
  auto checkpoint = entry.checkpoint;
  const auto outcome = adapter_.syncPhase(token, {entry, selected, reconcile, &checkpoint});
  observe(SyncCheckpoint::RemoteReturned, id, phase);
  if (outcome == PhaseOutcome::Uncertain) return SyncResult::Uncertain;
  if (outcome == PhaseOutcome::Checkpointed) {
    if (checkpoint == entry.checkpoint || journal_.checkpoint(id, checkpoint) != JournalResult::Ok)
      return storageError();
    observe(SyncCheckpoint::PhaseStored, id, phase);
    return SyncResult::Advanced;
  }
  if (outcome != PhaseOutcome::Confirmed) return SyncResult::Unavailable;
  if (journal_.acknowledge(id, selected, checkpoint) != JournalResult::Ok) return storageError();
  observe(SyncCheckpoint::AckStored, id, phase);
  return SyncResult::Advanced;
}
}  // namespace booksync
