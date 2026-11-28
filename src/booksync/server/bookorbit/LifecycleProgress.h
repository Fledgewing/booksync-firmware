// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "LifecycleMatch.h"

namespace booksync::server {
// Private normalized reply. Missing (GET 404), absent percentage and zero are
// distinct. Raw position bytes are never rewritten or interpreted as an anchor.
struct ProgressValues {
  bool present = false, percentagePresent = false;
  double percentage = 0;
  uint64_t timestamp = 0;
  std::string_view position, label;
  bool reset = false, web = false;
};
struct ProgressInput {
  storage::ScopeView scope;
  uint32_t syncOwner = 0;
  storage::SourceIdentity source;
  // Explicit durable intent chosen by the neutral policy owner. Default fetch
  // cannot authorize a write. This phase does not decide ahead/behind/consent.
  bool deliver = false;
  double percentage = 0;
  std::string_view position;
  uint64_t capturedAt = 0;
};
class ProgressEvidence {
 public:
  virtual ~ProgressEvidence() = default;
  // Immutable journal capture bound to this exact F08 source and account.
  // Position is the preserved protocol locator, not a synthesized percentage
  // anchor. Unknown native locators stay retained until a lossless mapping exists.
  virtual bool load(const storage::JournalEntry&, ProgressInput&) = 0;
  // Idempotently publish reply to durable account/source-scoped progress storage.
  // Recheck source/account; retain unknown native bytes. Never navigate or save
  // native reader position here. F19 consumes this data and owns position policy.
  virtual bool apply(const storage::JournalEntry&, const ProgressInput&, const ProgressValues&) = 0;
};
struct ProgressQuery {
  std::string_view hash;
  bool deliver = false;
  double percentage = 0;
  std::string_view position;
  uint64_t capturedAt = 0;
};
enum class ProgressResult : uint8_t { Ok, Uncertain, Unavailable };
struct ProgressReply {
  ProgressResult result = ProgressResult::Unavailable;
  ProgressValues values;
};
class ProgressTransport {
 public:
  virtual ~ProgressTransport() = default;
  // F15 sync login only: x-auth-user/x-auth-key, never App-Attest.
  // Fetch: GET /koreader/syncs/progress/<hash>; 404 => Ok absent.
  // Deliver: PUT /koreader/syncs/progress with document, percentage, progress,
  // device="BookSync", durable device_id and capturedAt as timestamp.
  // Preserve progress bytes (<=4096), optional percentage/timestamp, and bounded
  // device_name ?: device label (<=100). reset means device_id=="bookorbit-web"
  // and percentage==0; web means device=="web". Do not invent a timestamp.
  // Reconcile an uncertain PUT using GET and durable evidence of the original
  // result, or proof it never committed, before any resend. Equality alone is
  // not commit proof; never overwrite newer remote progress to recover an old
  // attempt. No server idempotency key is assumed. If proof is unavailable return
  // Uncertain. GET is safely repeatable. Malformed/truncated replies cannot pass.
  // Borrowed reply storage must not overlap the caller-owned checkpoint output.
  ProgressReply exchange(const SyncWriterToken&, const ProgressQuery&, bool reconcile);

 protected:
  virtual ProgressReply request(const SyncWriterToken&, std::string_view method, std::string_view path,
                                const ProgressQuery&, bool reconcile) = 0;
};
class LifecycleProgress final {
 public:
  static constexpr size_t HeaderBytes = 28;
  static constexpr size_t MaxPositionBytes = 4096;
  // Worker-owned bounded scratch; no heap allocation or large task-stack copy.
  // Unpersistable replies remain pending, never truncated to the journal limit.
  LifecycleProgress(Account& account, ProgressEvidence& evidence, ProgressTransport& transport, char* output,
                    size_t capacity)
      : account_(account), evidence_(evidence), transport_(transport), output_(output), capacity_(capacity) {}
  PhaseOutcome run(const SyncWriterToken&, const PhaseRequest&);
  static bool decode(std::string_view, std::string_view& matched, ProgressValues&);

 private:
  Account& account_;
  ProgressEvidence& evidence_;
  ProgressTransport& transport_;
  char* output_;
  size_t capacity_;
};
}  // namespace booksync::server
