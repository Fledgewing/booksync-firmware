// SPDX-License-Identifier: GPL-3.0-or-later
// Semantics ported from BookSync companion BookOrbitSync.kt annotations phase.
#include "LifecycleAnnotations.h"

#include <Logging.h>

#include <limits>

namespace booksync::server {
namespace {
bool sameAccount(const AccountSnapshot& a, const AccountSnapshot& b) {
  return b.syncReady && b.capabilities.sync && a.syncOwner == b.syncOwner && a.ticket.owner == b.ticket.owner &&
         a.ticket.epoch == b.ticket.epoch;
}
bool text(std::string_view s, size_t max) { return s.size() <= max && s.find('\0') == s.npos; }
bool date(std::string_view s) {
  if (s.size() != 19) return false;
  for (size_t i = 0; i < s.size(); ++i) {
    const char separator = i == 4 || i == 7 ? '-' : i == 10 ? ' ' : i == 13 || i == 16 ? ':' : 0;
    if (separator ? s[i] != separator : s[i] < '0' || s[i] > '9') return false;
  }
  return true;
}
bool row(const AnnotationRow& r, AnnotationKind kind) {
  return date(r.datetime) && (r.updated.empty() || date(r.updated)) && !r.start.empty() && text(r.start, 4000) &&
         text(r.end, 4000) && text(r.text, 10000) && text(r.note, kind == AnnotationKind::Bookmark ? 500 : 5000) &&
         text(r.title, 500) && text(r.chapter, 500) && text(r.drawer, 32) && text(r.color, 32) &&
         (kind == AnnotationKind::Bookmark || r.format == "xpointer" || r.format == "page") && !r.pendingVerification;
}
const AnnotationChange* changeAt(const AnnotationChange* rows, AnnotationChangeReader* reader, size_t i) {
  return reader ? reader->at(i) : rows ? rows + i : nullptr;
}
bool changes(const AnnotationChange* rows, AnnotationChangeReader* reader, size_t count, AnnotationKind kind) {
  if (count > LifecycleAnnotations::MaxRemoteChanges || (count > LifecycleAnnotations::MaxChanges && !reader))
    return false;
  for (size_t i = 0; i < count; ++i) {
    const auto* c = changeAt(rows, reader, i);
    if (!c || c->op > AnnotationOp::Delete || !c->row.serverId ||
        c->row.serverId > uint64_t(std::numeric_limits<int64_t>::max()) ||
        c->row.version > uint64_t(std::numeric_limits<int64_t>::max()) || !text(c->row.key, 32))
      return false;
    if (c->op == AnnotationOp::Delete) continue;
    const auto& r = c->row;
    // Server-created bookmarks omit datetime; highlight edits may omit locators
    // and posFormat. Preserve presence instead of treating these as local captures.
    if ((!r.datetime.empty() && !date(r.datetime)) || (!r.updated.empty() && !date(r.updated)) ||
        (c->op == AnnotationOp::Add && r.start.empty()) || !text(r.start, 4000) || !text(r.end, 4000) ||
        !text(r.format, 32) || !text(r.text, 10000) || !text(r.note, 5000) || !text(r.title, 500) ||
        !text(r.chapter, 500) || !text(r.drawer, 32) || !text(r.color, 32))
      return false;
    if (kind == AnnotationKind::Bookmark && (r.fields & Title) && !text(r.title, 500)) return false;
  }
  return true;
}
PhaseOutcome unavailable() {
  LOG_ERR("BSA", "Annotations unavailable; pending work retained");
  return PhaseOutcome::Unavailable;
}
PhaseOutcome uncertain() {
  LOG_INF("BSA", "Annotations pending; reconcile before phase acknowledgement");
  return PhaseOutcome::Uncertain;
}
}  // namespace
PhaseOutcome LifecycleAnnotations::run(const SyncWriterToken& token, const PhaseRequest& request) {
  if (request.phase != storage::LifecyclePhase::Annotations) return PhaseOutcome::Unavailable;
  const auto account = account_.snapshot();
  // MatchCheckpoint (~128 bytes) and capture (~104 bytes) borrow strings; no
  // annotation payload arrays, encoded copies or heap allocations on task stack.
  MatchCheckpoint match;
  if (!account.syncReady || !account.capabilities.sync || !request.confirmedCheckpoint ||
      !LifecycleMatch::decode(request.entry.checkpoint, match))
    return unavailable();
  if (!match.positionCompatible) return PhaseOutcome::Confirmed;
  AnnotationCapture capture;
  if (!workspace_.begin(request.entry, match, capture) || capture.scope.server != request.entry.adapter ||
      capture.scope.account != request.entry.owner || capture.source != match.source ||
      capture.syncOwner != account.syncOwner || !sameAccount(account, account_.snapshot()))
    return unavailable();
  const bool enabled = capture.enabled && capture.legacyEnabled;
  const bool complete = capture.native && capture.closed && capture.present && capture.syncKnown;
  bool incomplete = false;
  for (const auto kind : {AnnotationKind::Highlight, AnnotationKind::Bookmark}) {
    AnnotationProgress progress;
    AnnotationPending pending;
    if (!workspace_.load(kind, progress, pending)) return unavailable();
    if (pending.exists) {
      if (pending.next.cursor < progress.cursor ||
          uint64_t(pending.next.cursor) > uint64_t(progress.cursor) + MaxChanges ||
          !changes(pending.changes, pending.reader, pending.count, kind))
        return unavailable();
      if (!capture.closed || !capture.present) {
        incomplete = true;
        continue;
      }
      // load provides borrowed pending rows; apply must not invalidate those views.
      if (!sameAccount(account, account_.snapshot())) return uncertain();
      AnnotationApplied applied;
      if (!workspace_.apply(kind, pending, applied) || applied.count != pending.count ||
          (applied.count > MaxChanges && !applied.reader))
        return unavailable();
      for (size_t i = 0; i < applied.count; ++i) {
        const auto* change = changeAt(pending.changes, pending.reader, i);
        if (!change) return unavailable();
        // Copy only identity before advancing another reusable decoder view.
        const auto id = change->row.serverId, version = change->row.version;
        const auto op = change->op;
        const auto* receipt = applied.reader ? applied.reader->at(i) : applied.rows ? applied.rows + i : nullptr;
        if (!receipt || !receipt->change || receipt->change->row.serverId != id ||
            receipt->change->row.version != version || receipt->change->op != op || receipt->verified ||
            (op == AnnotationOp::Delete && !receipt->applied))
          return unavailable();
      }
      if (!sameAccount(account, account_.snapshot())) return uncertain();
      const auto result = transport_.acknowledge(token, kind, match.hash, applied);
      if (!sameAccount(account, account_.snapshot()) || result == MatchResult::Uncertain) return uncertain();
      if (result != MatchResult::Ok) return unavailable();
      progress = pending.next;
      if (!workspace_.finish(kind, progress)) return unavailable();
    }
    if (progress.done || !enabled) continue;
    AnnotationBatch batch;
    if (!workspace_.batch(kind, progress.cursor, batch) || batch.count > MaxChanges ||
        (batch.count && !batch.changes) || batch.keyCount > MaxKeys || (batch.keyCount && !batch.keys) ||
        batch.count > std::numeric_limits<uint32_t>::max() - progress.cursor || (!batch.count && !batch.last))
      return unavailable();
    for (size_t i = 0; i < batch.count; ++i)
      if (!row(batch.changes[i], kind)) return unavailable();
    // Full keys only accompany the first chunk of a known current closed capture.
    // Subsequent requests preserve keysComplete but do not repeat the key array.
    if (!complete || progress.cursor != 0) {
      batch.keys = nullptr;
      batch.keyCount = 0;
    }
    for (size_t i = 0; i < batch.keyCount; ++i) {
      const auto& key = batch.keys[i];
      if (key.key.size() != 32 || !date(key.datetime)) return unavailable();
      for (char c : key.key)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return unavailable();
    }
    if (!sameAccount(account, account_.snapshot())) return uncertain();
    const auto reply = transport_.exchange(token, kind, match.hash, batch, complete, request.reconcile);
    if (!sameAccount(account, account_.snapshot()) || reply.result == MatchResult::Uncertain) return uncertain();
    if (reply.result != MatchResult::Ok || !changes(reply.changes, reply.reader, reply.count, kind))
      return unavailable();
    const AnnotationProgress next{progress.cursor + static_cast<uint32_t>(batch.count), batch.last && !reply.more};
    if (reply.count) {
      if (!workspace_.stage(kind, reply, next)) return unavailable();
      // Always recover through the staged reply, even on this same invocation.
      // One bounded exchange per kind per step; pending applications continue on
      // the next serial owner step. F17 keeps its durable attempt marker meanwhile.
      incomplete = true;
    } else {
      if (!workspace_.finish(kind, next)) return unavailable();
      incomplete |= !next.done;
    }
  }
  if (incomplete) return uncertain();
  if (enabled && capture.closed && capture.present && !workspace_.markKnown()) return unavailable();
  if (!sameAccount(account, account_.snapshot())) return uncertain();
  // Preserve BSM1 and raw capture unchanged for the later state/progress phases.
  return PhaseOutcome::Confirmed;
}
bool LifecycleAnnotations::matches(const AnnotationRow& slot, const AnnotationRow& change) {
  return (slot.serverId && slot.serverId == change.serverId) ||
         (!change.datetime.empty() && slot.datetime == change.datetime && slot.start == change.start) ||
         (!change.key.empty() && slot.key == change.key);
}
bool LifecycleAnnotations::merge(AnnotationKind kind, AnnotationRow& slot, const AnnotationRow& incoming,
                                 bool existing) {
  if (kind == AnnotationKind::Highlight && !existing && incoming.format != "xpointer") return false;
  slot.serverId = incoming.serverId;
  slot.version = incoming.version;
  if (incoming.fields & Date) slot.datetime = incoming.datetime;
  if (incoming.fields & Updated) slot.updated = incoming.updated;
  if (incoming.fields & Start) slot.start = incoming.start;
  if (incoming.fields & End) slot.end = incoming.end;
  if (incoming.fields & Format) slot.format = incoming.format;
  if (incoming.fields & Drawer) slot.drawer = incoming.drawer;
  if (incoming.fields & Color) slot.color = incoming.color;
  if ((incoming.fields & Text) && !incoming.text.empty()) slot.text = incoming.text;
  if (incoming.fields & Chapter) slot.chapter = incoming.chapter;
  if (incoming.fields & Page) slot.page = incoming.page;
  slot.note = incoming.note;
  slot.pendingVerification = kind == AnnotationKind::Highlight;
  if (kind == AnnotationKind::Bookmark) {
    slot.drawer = {};
    if (incoming.fields & Title) slot.note = incoming.title;
  }
  return true;
}
}  // namespace booksync::server

// Keep the production phase reachable for early full-link size measurements.
extern "C" {
struct AnnotationLinkAnchor {
  decltype(&booksync::server::LifecycleAnnotations::run) run;
  decltype(&booksync::server::LifecycleAnnotations::matches) matches;
  decltype(&booksync::server::LifecycleAnnotations::merge) merge;
};
extern const AnnotationLinkAnchor booksync_annotations_link_anchor = {&booksync::server::LifecycleAnnotations::run,
                                                                      &booksync::server::LifecycleAnnotations::matches,
                                                                      &booksync::server::LifecycleAnnotations::merge};
}
