// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "LifecycleMatch.h"

namespace booksync::server {
// Normalized private wire state. Known false means absent/unsupported, whereas
// known true with rating 0 or reviewSet false means an explicit remote clear.
// statusKnown is statusSet==true; rating/review known means the respective
// ratingSet/reviewNoteSet wire field is Boolean. Normalize statusUpdatedAt to
// its first ten date bytes; missing fields remain unknown.
struct StateValues {
  bool statusKnown = false;
  std::string_view status, statusModified;
  bool ratingKnown = false;
  uint8_t rating = 0;
  bool reviewKnown = false, reviewSet = false;
  std::string_view reviewNote;
};
struct StateInput {
  storage::ScopeView scope;
  uint32_t syncOwner = 0;
  storage::SourceIdentity source;
  std::string_view status, statusModified;
  uint8_t rating = 0;  // Absent capture, not automatically a clear.
  bool reviewPresent = false;
  std::string_view reviewNote, reviewModified;
  StateValues known;  // Same account/document's durable history, never another login's.
  bool manualStatusPending = false, open = false;
  std::string_view capturedToken, currentToken;
};
class StateEvidence {
 public:
  virtual ~StateEvidence() = default;
  // Load immutable journal capture and account-scoped history, not current values
  // from a reused path. Verify exact source/document and retain unknown raw data.
  virtual bool load(const storage::JournalEntry&, StateInput&) = 0;
  // Idempotent durable writeback + known-history publication for this entry.
  // Caller excludes open books. Provider must atomically recheck open/source and
  // native status token: status applies only when !manualStatusPending and the
  // current native token still equals capturedToken. Ratings/reviews apply only
  // when known. Persist rating/review history flags even when false, so unknown
  // replies cannot leave stale certainty. Preserve unrelated native fields;
  // return false on any failure.
  // Replay after a cut between publication and phase ack must be harmless.
  virtual bool apply(const storage::JournalEntry&, const StateInput&, const StateValues&) = 0;
};
struct StateQuery {
  std::string_view hash, status, statusModified;
  uint8_t rating = 0;
  bool ratingCleared = false, reviewPresent = false;
  std::string_view reviewNote, reviewModified;
  bool reviewCleared = false;
};
enum class StateResult : uint8_t { Ok, Uncertain, Unavailable };
struct StateReply {
  StateResult result = StateResult::Unavailable;
  StateValues values;
};
class StateTransport {
 public:
  virtual ~StateTransport() = default;
  // POST /koreader/plugin/book-states, private device envelope + one books row.
  // Emit only present fields; statusModified only accompanies status, absent
  // rating/review never means clear without the explicit clear flags. Select
  // the exact hash's result; missing/ambiguous/truncated replies are Uncertain.
  // Reconciliation must recover the original result or prove it uncommitted
  // before resending unchanged captured values. No invented idempotency header
  // and no blind replay: an unknown delivery stays Uncertain. Never mutate a
  // newer remote state just to reconcile an older attempt.
  virtual StateReply exchange(const SyncWriterToken&, const StateQuery&, bool reconcile) = 0;
};
class LifecycleState final {
 public:
  static constexpr size_t HeaderBytes = 14;
  static constexpr size_t MaxReviewBytes = 10000;
  // Worker-owned output, disjoint from evidence/transport/journal scratch; no
  // additional heap or 1 KiB local task-stack buffer. Default adapter stays offline.
  LifecycleState(Account& account, StateEvidence& evidence, StateTransport& transport, char* output, size_t capacity)
      : account_(account), evidence_(evidence), transport_(transport), output_(output), capacity_(capacity) {}
  PhaseOutcome run(const SyncWriterToken&, const PhaseRequest&);
  static bool decode(std::string_view bytes, std::string_view& matched, StateValues&);

 private:
  Account& account_;
  StateEvidence& evidence_;
  StateTransport& transport_;
  char* output_;
  size_t capacity_;
};
}  // namespace booksync::server
