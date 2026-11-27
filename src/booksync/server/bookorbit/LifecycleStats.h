// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "LifecycleMatch.h"
#include "booksync/storage/PageJournal.h"
namespace booksync::server {
struct StatsInput {
  storage::ScopeView scope;
  uint32_t syncOwner = 0;
  storage::SourceIdentity source;
  storage::PageJournal* history = nullptr;
  uint64_t open = 0, capturedAt = 0;
  uint32_t begin = 0, end = 0;  // Immutable half-open source snapshot, never live count.
  bool metadataAmbiguous = false;
};
class StatsEvidence {
 public:
  virtual ~StatsEvidence() = default;
  // Bind this exact captured F09 open/range to the F18a source revision and
  // account. No path reuse, new source rows, scan or mutable live cutoff.
  virtual bool load(const storage::JournalEntry&, StatsInput&) = 0;
};
struct StatsEvent {
  uint32_t page = 0;
  uint64_t startTime = 0;
  uint32_t durationSeconds = 0, totalPages = 0;
};
struct StatsQuery {
  std::string_view hash;
  uint64_t bookFileId = 0;
  const StatsEvent* events = nullptr;
  size_t count = 0;
};
enum class StatsResult : uint8_t { Ok, Uncertain, Unavailable };
struct StatsReply {
  StatsResult result = StatsResult::Unavailable;
  std::string_view hash;
  uint32_t accepted = 0, duplicates = 0;
  uint64_t watermark = 0;
  bool unmatched = false, positionIncompatible = false;
};
class StatsTransport {
 public:
  virtual ~StatsTransport() = default;
  // POST /koreader/plugin/page-stats, books:[{hash,bookFileId,events}], with
  // the companion's device envelope and F15 sync login (never account/OPDS).
  // The server deduplicates (device,file,startTime,page), deriving sessions,
  // reading time and daily totals transactionally. Reconciliation replays ONLY
  // the identical frozen rows. No synthetic session API or idempotency header.
  // Ok requires a complete, bounded decoded result for this hash, no unmatched
  // or positionIncompatible entry. Transport/body ambiguity is Uncertain.
  virtual StatsReply upload(const SyncWriterToken&, const StatsQuery&, bool reconcile) = 0;
};
class LifecycleStats final {
 public:
  static constexpr size_t Batch = 32, HeaderBytes = 36;
  // Fixed caller-owned batch/output, reused for worker lifetime, no heap churn.
  // They must not overlap journal scratch, evidence or one another; all borrowed
  // storage must outlive the synchronous owner call.
  LifecycleStats(Account& account, StatsEvidence& evidence, StatsTransport& transport, StatsEvent* batch,
                 size_t batchCapacity, char* output, size_t capacity)
      : account_(account),
        evidence_(evidence),
        transport_(transport),
        batch_(batch),
        batchCapacity_(batchCapacity),
        output_(output),
        capacity_(capacity) {}
  PhaseOutcome run(const SyncWriterToken&, const PhaseRequest&);
  static bool decode(std::string_view, std::string_view& matched, uint32_t& cursor, uint32_t& begin, uint32_t& end,
                     uint64_t& open, uint64_t& capturedAt);

 private:
  Account& account_;
  StatsEvidence& evidence_;
  StatsTransport& transport_;
  StatsEvent* batch_;
  size_t batchCapacity_;
  char* output_;
  size_t capacity_;
};
}  // namespace booksync::server
