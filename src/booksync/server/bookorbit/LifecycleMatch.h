// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/services/Account.h"
#include "booksync/services/ServerAdapter.h"
#include "booksync/storage/Identity.h"

namespace booksync::server {
// Borrowed, immutable captured evidence from the worker's bounded index window.
// load must tie the protocol digest/metadata to this exact captured revision and
// document, not the current bytes at a reused path. Never derive an F08 UUID from
// the partial MD5 used by the protocol. No scan, hash or file I/O during paint.
struct MatchInput {
  storage::ScopeView scope;
  uint32_t syncOwner = 0;  // F15 process-local connector identity, not remote ID.
  storage::SourceIdentity source;
  std::string_view path, hash, title, authors;
  uint64_t lastOpen = 0;  // Optional Unix time; zero means absent.
  const storage::BindingView* rows = nullptr;
  size_t count = 0;
  const storage::BookAliasBindingView* aliases = nullptr;
  size_t aliasCount = 0;
};
class MatchEvidence {
 public:
  virtual ~MatchEvidence() = default;
  virtual bool load(const storage::JournalEntry&, MatchInput&) = 0;
};
struct MatchQuery {
  std::string_view hash, title, authors;
  uint64_t lastOpen = 0;
  std::string_view source = "current_file";
};
struct MatchRow {
  std::string_view hash;
  uint64_t bookId = 0, bookFileId = 0;
  bool positionCompatible = true;  // Wire omission means compatible.
  bool operator==(const MatchRow&) const = default;
};
enum class MatchResult : uint8_t { Ok, Uncertain, Unavailable };
struct MatchReply {
  MatchResult result = MatchResult::Unavailable;
  const MatchRow* rows = nullptr;
  size_t count = 0;
};
class MatchTransport {
 public:
  virtual ~MatchTransport() = default;
  // POST /koreader/plugin/match-check with hashes:[hash], books:[query] and
  // the private device envelope, using only the F15 sync login. Reconciliation
  // repeats this hash association lookup, which must return an existing binding
  // before creating one. Never invent an idempotency header or blindly replay a
  // different mutation. Timeouts, including after commit, remain Uncertain.
  // Bounded decoded rows are borrowed until the next call; no truncated success.
  virtual MatchReply check(const SyncWriterToken&, const MatchQuery&, bool reconcile) = 0;
};
struct MatchCheckpoint {
  storage::SourceIdentity source;
  uint64_t bookId = 0, bookFileId = 0;
  bool positionCompatible = false;
  std::string_view hash, native;
};
// Phase handler only: SyncOwner alone marks attempts and commits checkpoints.
// Caller-owned output avoids another 1 KiB task-stack buffer or heap allocation.
// Scope strings, evidence and output must outlive the synchronous owner call.
// Output must not overlap evidence or decoded transport rows.
class LifecycleMatch final {
 public:
  static constexpr size_t MaxRows = 8;
  static constexpr size_t HeaderBytes = 119;
  LifecycleMatch(Account& account, MatchEvidence& evidence, MatchTransport& transport, char* output, size_t capacity)
      : account_(account), evidence_(evidence), transport_(transport), output_(output), capacity_(capacity) {}
  PhaseOutcome run(const SyncWriterToken&, const PhaseRequest&);
  // Adapter-private checkpoint, not a global identity-store merge or replay proof.
  // Raw native checkpoint bytes survive intact for subsequent lifecycle phases.
  static bool decode(std::string_view, MatchCheckpoint&);

 private:
  Account& account_;
  MatchEvidence& evidence_;
  MatchTransport& transport_;
  char* output_;
  size_t capacity_;
};
}  // namespace booksync::server
