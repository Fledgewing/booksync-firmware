// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ServerAdapter.h"

namespace booksync {
enum class SyncResult : uint8_t;
enum class ReadingStatus : uint8_t { Unknown, Unread, Reading, Finished, OnHold, Abandoned, Rereading };
struct FinishValues {
  ReadingStatus status = ReadingStatus::Unknown;
  bool ratingKnown = false;
  uint8_t rating = 0;  // Known zero explicitly clears; unknown never clears.
};
struct FinishIntent {
  uint64_t position = 0, undoOf = 0;
  FinishValues before, desired;
  std::string_view day;  // Empty when capture has no trustworthy date.
};
// Provider must prove this exact immutable account/document capture performed
// outgoing position delivery in F18e. A successful fetch is not this proof.
class FinishPositionProof {
 public:
  virtual ~FinishPositionProof() = default;
  virtual bool delivered(const storage::JournalEntry&) const = 0;
};
// Worker-only, externally serialized with every other journal user. Immutable
// captures and completed receipts share the lifecycle journal. Receipts are kept
// for restart-safe Undo/dedup; capacity blocks new intents, never evicts history.
// Borrowed scratch must be >= SyncOwner::ScratchBytes and disjoint from its
// scratch. Reuse worker storage, never allocate per intent or on the task stack.
class Finish final {
 public:
  Finish(storage::LifecycleJournal& journal, FinishPositionProof& proof, char* scratch, size_t capacity)
      : journal_(journal), proof_(proof), scratch_(scratch), capacity_(capacity) {}
  storage::JournalResult enqueue(uint64_t position, std::string_view adapter, std::string_view account,
                                 std::string_view document, FinishValues before, FinishValues desired,
                                 std::string_view day, uint64_t& id);
  storage::JournalResult undo(uint64_t id, std::string_view adapter, std::string_view account, uint64_t& undoId);
  static bool recognizes(std::string_view payload) { return payload.substr(0, 4) == "BSFI"; }
  static bool decode(std::string_view payload, FinishIntent& out);
  static bool settled(const storage::JournalEntry&);

 private:
  friend class SyncOwner;
  storage::LifecycleJournal& journal_;
  FinishPositionProof& proof_;
  char* scratch_;
  size_t capacity_;
  storage::JournalResult positionPlan(const storage::JournalEntry&, bool& suppressState, bool& cancelPosition);
  storage::JournalResult positionDelivered(const storage::JournalEntry&);
  SyncResult step(const SyncWriterToken&, ServerAdapter&, const storage::JournalEntry&);
  bool ready() const;
};
// Each operation uses the sole writer token and scoped immutable event ID.
// State (finish day) -> Status (guarded against a newer remote choice) -> Rating
// (after status, including explicit clear) -> Apply (idempotent local writeback).
// Undo is a compensating intent with expected=original desired; reconciliation
// must prove the original result or noncommit before resending. Equality alone
// is not proof. Never overwrite a newer remote intent to implement Undo.
struct FinishRequest {
  const storage::JournalEntry& entry;
  FinishIntent intent;
  storage::LifecyclePhase phase;  // Stats=State, Annotations=Status, State=Rating, Progress=Apply.
  bool reconcile;
  // Adapter-owned bounded output, consumed synchronously on Confirmed or
  // Checkpointed. Preserve resolved identity inside any opaque result envelope.
  // Checkpointed saves a result before local Apply without acknowledging the
  // operation; unknown/unavailable replies retain the previous durable bytes.
  std::string_view* confirmedCheckpoint = nullptr;
};
}  // namespace booksync
