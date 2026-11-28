// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "LifecycleMatch.h"

namespace booksync::server {
enum class AnnotationKind : uint8_t { Highlight, Bookmark };
enum class AnnotationOp : uint8_t { Add, Edit, Delete };
// Borrowed protocol values, never exposed by Library. Missing note means clear;
// missing/empty text preserves the existing passage. Raw locators are retained.
enum AnnotationField : uint16_t {
  Date = 1 << 0,
  Updated = 1 << 1,
  Start = 1 << 2,
  End = 1 << 3,
  Format = 1 << 4,
  Drawer = 1 << 5,
  Color = 1 << 6,
  Text = 1 << 7,
  Chapter = 1 << 8,
  Page = 1 << 9,
  Title = 1 << 10,
  AllFields = (1 << 11) - 1
};
struct AnnotationRow {
  uint64_t serverId = 0, version = 0;
  std::string_view datetime, updated, start, end, format, drawer, color, text, note, chapter, title, key;
  uint32_t page = 0;
  uint16_t fields = AllFields;  // Decoders retain presence; absent note still clears.
  bool pendingVerification = false;
};
struct AnnotationChange {
  AnnotationOp op = AnnotationOp::Add;
  AnnotationRow row;
};
struct AnnotationKey {
  std::string_view key, datetime;  // MD5(datetime + '|' + raw locator), Latin-1 protocol encoding.
};
struct AnnotationCapture {
  storage::ScopeView scope;
  storage::SourceIdentity source;
  uint32_t syncOwner = 0;
  bool enabled = true, legacyEnabled = true;
  bool native = false, closed = false, present = false, syncKnown = false;
};
// Frozen SD captures can contain large values and a complete 1024-key set.
// at(0) rewinds; later reads are sequential. One record is reused and its views
// survive only until the next at(). Arrays remain supported for small sources.
class AnnotationRowReader {
 public:
  virtual ~AnnotationRowReader() = default;
  virtual const AnnotationRow* at(size_t index) = 0;
};
class AnnotationKeyReader {
 public:
  virtual ~AnnotationKeyReader() = default;
  virtual const AnnotationKey* at(size_t index) = 0;
};
struct AnnotationBatch {
  const AnnotationRow* changes = nullptr;
  size_t count = 0;
  const AnnotationKey* keys = nullptr;
  size_t keyCount = 0;
  bool last = true;
  AnnotationRowReader* rowReader = nullptr;
  AnnotationKeyReader* keyReader = nullptr;
  // Distinguish a known-empty first snapshot from omitted keys on a later
  // exchange round, including when there were no outgoing rows to move cursor.
  bool keysIncluded = true;
};
// Large server push-downs are spooled/decoded through one reusable record.
// at(0) rewinds; subsequent calls are sequential. No full response allocation.
class AnnotationChangeReader {
 public:
  virtual ~AnnotationChangeReader() = default;
  virtual const AnnotationChange* at(size_t index) = 0;
};
struct AnnotationReply {
  MatchResult result = MatchResult::Unavailable;
  const AnnotationChange* changes = nullptr;
  size_t count = 0;
  bool more = false;
  AnnotationChangeReader* reader = nullptr;
};
struct AnnotationReceipt {
  const AnnotationChange* change = nullptr;
  bool applied = false;
  bool verified = false;
};
class AnnotationReceiptReader {
 public:
  virtual ~AnnotationReceiptReader() = default;
  virtual const AnnotationReceipt* at(size_t index) = 0;
};
struct AnnotationApplied {
  const AnnotationReceipt* rows = nullptr;
  size_t count = 0;
  AnnotationReceiptReader* reader = nullptr;
};
struct AnnotationProgress {
  uint32_t cursor = 0;
  bool done = false;
};
struct AnnotationPending {
  bool exists = false;
  AnnotationProgress next;
  const AnnotationChange* changes = nullptr;
  size_t count = 0;
  AnnotationChangeReader* reader = nullptr;
};
// Adapter-private durable workspace, scoped by the FULL entry tuple and matched
// source. begin freezes captured rows once (including empty captures); batch must
// never recapture on restart. Historical/open snapshots cannot prove absence.
// Returned batch views survive exchange; pending views survive apply. Other
// returned views remain valid until the next workspace call. Every mutation
// is atomic, recoverable and fallible; implementations use FilePublish, not a
// mutable native reader handle. Damaged/unknown workspaces fail closed.
class AnnotationWorkspace {
 public:
  virtual ~AnnotationWorkspace() = default;
  virtual bool begin(const storage::JournalEntry&, const MatchCheckpoint&, AnnotationCapture&) = 0;
  virtual bool load(AnnotationKind, AnnotationProgress&, AnnotationPending&) = 0;
  virtual bool batch(AnnotationKind, uint32_t cursor, AnnotationBatch&) = 0;
  // Persist the ENTIRE remote reply and next cursor before applying or acking.
  virtual bool stage(AnnotationKind, const AnnotationReply&, AnnotationProgress next) = 0;
  // Idempotent closed-file transaction: match by server ID, datetime+locator or
  // protocol key; add/edit/delete in that order; preserve unrelated native data.
  // Persist verification records before returning success. Return one receipt per
  // pending row (including failed new page highlights), with verified=false.
  // Receipts/rows survive until the next workspace call. No ack on failure.
  virtual bool apply(AnnotationKind, const AnnotationPending&, AnnotationApplied&) = 0;
  // Atomically advance the cursor/done bit and retire pending only AFTER ack.
  virtual bool finish(AnnotationKind, AnnotationProgress) = 0;
  virtual bool markKnown() = 0;
};
class AnnotationTransport {
 public:
  virtual ~AnnotationTransport() = default;
  // POST /koreader/plugin/{annotations,bookmarks}/exchange; private F15 device
  // envelope + sync login. Dedup is the protocol datetime/position key, never an
  // invented HTTP idempotency header. Reconcile an uncertain exchange using the
  // SAME frozen batch and keys. Reject unmatched/incompatible/partial responses.
  virtual AnnotationReply exchange(const SyncWriterToken&, AnnotationKind, std::string_view hash,
                                   const AnnotationBatch&, bool keysComplete, bool reconcile) = 0;
  // POST corresponding exchange-ack after durable local application. Highlights
  // are applied with verified:false until native anchor verification succeeds;
  // new non-xpointer highlights fail. Bookmarks ack applied with key/date/pos;
  // deleted rows ack applied even if already absent. Repeating an ack is safe.
  virtual MatchResult acknowledge(const SyncWriterToken&, AnnotationKind, std::string_view hash,
                                  const AnnotationApplied&) = 0;
};
class LifecycleAnnotations final {
 public:
  static constexpr size_t MaxChanges = 16, MaxRemoteChanges = 600, MaxKeys = 1024;
  // Push-downs may contain 100 adds plus up to 500 bookmark tombstones.
  // Responses/receipts larger than 16 MUST stream through their readers.
  LifecycleAnnotations(Account& account, AnnotationWorkspace& workspace, AnnotationTransport& transport)
      : account_(account), workspace_(workspace), transport_(transport) {}
  PhaseOutcome run(const SyncWriterToken&, const PhaseRequest&);
  // Pure merge for an already identity-matched slot; false = failed application.
  // A delete is handled by the workspace's native transaction, including absent
  // slots. Verification-pending rows never become outgoing local changes.
  static bool matches(const AnnotationRow&, const AnnotationRow&);
  static bool merge(AnnotationKind, AnnotationRow& slot, const AnnotationRow& incoming, bool existing);

 private:
  Account& account_;
  AnnotationWorkspace& workspace_;
  AnnotationTransport& transport_;
};
}  // namespace booksync::server
