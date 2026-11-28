#pragma once

#include "booksync/interface/Capabilities.h"
#include "booksync/interface/Commands.h"
#include "booksync/interface/Models.h"
#include "booksync/services/AuthAdapter.h"
#include "booksync/storage/LifecycleJournal.h"

namespace booksync {
class SyncOwner;
struct FinishRequest;
class CatalogSource;
class DownloadSource;
struct RemoteReadingStats;
// Only the serial owner can mint this capability. No public/default constructor,
// copying or moving; adapter writes must require the live owner's borrowed token.
class SyncWriterToken final {
  friend class SyncOwner;
  SyncWriterToken() = default;

 public:
  SyncWriterToken(const SyncWriterToken&) = delete;
  SyncWriterToken& operator=(const SyncWriterToken&) = delete;
};

enum class PhaseOutcome : uint8_t { Confirmed, Uncertain, Unavailable, Checkpointed };
struct PhaseRequest {
  const storage::JournalEntry& entry;
  storage::LifecyclePhase phase;
  bool reconcile;  // Prior attempt may have committed. Never blindly replay.
  // Optional adapter-owned bounded output, borrowed through acknowledge(). The
  // owner consumes it on Confirmed or Checkpointed. Checkpointed persists an
  // incomplete phase without acknowledging it and reports forward progress.
  // Unchanged partial output is rejected; attempts still require reconciliation.
  // Uncertain/unavailable outcomes retain the prior bytes.
  std::string_view* confirmedCheckpoint = nullptr;
};
// Checkpointed means a durable partial batch, never phase completion.
// Worker-only contract. No protocol DTO, session or transport escapes here.
// Neutral credential captures enter only through the private auth boundary.
// Phase adapters must deduplicate by (adapter, owner, entry.id, phase), or reconcile
// an uncertain attempt before writing. Confirmed means a durable remote commit.
class ServerAdapter : public AuthAdapter {
 public:
  virtual ~ServerAdapter() = default;
  virtual Capabilities capabilities() const = 0;
  virtual const char* serviceLabel() const { return ""; }
  virtual Snapshot cachedSnapshot() const = 0;
  // Normalized saved account statistics; no network or fetch-time inclusion inference.
  // False leaves the caller's result absent. Publication is separate from F18b delivery.
  virtual bool cachedReadingStats(RemoteReadingStats&) const { return false; }
  // Borrowed worker cursor, absent until authenticated catalog transport is available.
  virtual CatalogSource* catalogSource() { return nullptr; }
  virtual DownloadSource* downloadSource() { return nullptr; }
  virtual PhaseOutcome syncPhase(const SyncWriterToken&, const PhaseRequest&) { return PhaseOutcome::Unavailable; }
  virtual PhaseOutcome finishPhase(const SyncWriterToken&, const FinishRequest&) { return PhaseOutcome::Unavailable; }
  virtual CommandStatus deliver(const SyncWriterToken& token, Intent intent) = 0;
};

// Concrete wiring belongs to the adapter; bootstrap never names a vendor.
ServerAdapter& defaultServerAdapter();
}  // namespace booksync
