// SPDX-License-Identifier: GPL-3.0-or-later
// Independently implemented from BookSync companion BookOrbitSync.kt stats phase.
#include "LifecycleStats.h"

#include <Logging.h>

#include <algorithm>
#include <cstring>
namespace booksync::server {
namespace {
void put(char* p, uint64_t n, unsigned bytes) {
  for (unsigned i = 0; i < bytes; ++i) p[i] = char(n >> (8 * i));
}
uint64_t get(const char* p, unsigned bytes) {
  uint64_t n = 0;
  for (unsigned i = 0; i < bytes; ++i) n |= uint64_t(uint8_t(p[i])) << (8 * i);
  return n;
}
bool current(const AccountSnapshot& a, const AccountSnapshot& b) {
  return b.syncReady && a.syncOwner == b.syncOwner && a.ticket.owner == b.ticket.owner &&
         a.ticket.epoch == b.ticket.epoch;
}
PhaseOutcome unavailable() {
  LOG_ERR("BSST", "Stats unavailable; capture/checkpoint retained");
  return PhaseOutcome::Unavailable;
}
}  // namespace
bool LifecycleStats::decode(std::string_view bytes, std::string_view& matched, uint32_t& cursor, uint32_t& begin,
                            uint32_t& end, uint64_t& open, uint64_t& capturedAt) {
  if (bytes.size() < HeaderBytes || bytes.size() > storage::LifecycleJournal::MaxCheckpoint ||
      bytes.substr(0, 4) != "BST1")
    return false;
  cursor = get(bytes.data() + 4, 4);
  begin = get(bytes.data() + 8, 4);
  end = get(bytes.data() + 12, 4);
  open = get(bytes.data() + 16, 8);
  capturedAt = get(bytes.data() + 24, 8);
  if (!open || begin > cursor || cursor > end || end > storage::PageJournal::MaxEvents + 1 ||
      get(bytes.data() + 32, 4) != bytes.size() - HeaderBytes)
    return false;
  matched = bytes.substr(HeaderBytes);
  if (matched.substr(0, 4) != "BSM1") return false;
  MatchCheckpoint match;
  return LifecycleMatch::decode(matched, match);
}
PhaseOutcome LifecycleStats::run(const SyncWriterToken& token, const PhaseRequest& request) {
  using namespace storage;
  if (request.phase != LifecyclePhase::Stats) return unavailable();
  const auto account = account_.snapshot();
  auto matched = request.entry.checkpoint;
  uint32_t cursor = 0, begin = 0, end = 0;
  uint64_t open = 0, captured = 0;
  const bool resumed = matched.substr(0, 4) == "BST1";
  if (resumed && !decode(request.entry.checkpoint, matched, cursor, begin, end, open, captured)) return unavailable();
  MatchCheckpoint match;
  StatsInput input;
  if (!account.syncReady || !account.capabilities.sync || !request.confirmedCheckpoint || !batch_ ||
      batchCapacity_ < Batch || !output_ || matched.size() > LifecycleJournal::MaxCheckpoint - HeaderBytes ||
      capacity_ < HeaderBytes + matched.size() || !LifecycleMatch::decode(matched, match) ||
      !evidence_.load(request.entry, input) || input.syncOwner != account.syncOwner ||
      input.scope.server != request.entry.adapter || input.scope.account != request.entry.owner ||
      input.source != match.source || !input.history || !input.history->available() || !input.open ||
      !input.capturedAt || input.begin > input.end || input.end > input.history->count() ||
      (input.history->capped() && input.end == input.history->count()))
    return unavailable();
  if (resumed) {
    if (begin != input.begin || end != input.end || open != input.open || captured != input.capturedAt)
      return unavailable();
  } else {
    cursor = begin = input.begin;
    end = input.end;
    open = input.open;
    captured = input.capturedAt;
  }
  size_t count = 0;
  uint32_t next = input.metadataAmbiguous ? end : cursor;
  uint64_t watermark = 0;
  // Bound disk work even across other books/gaps; progress is a durable source-row
  // cursor, so equal timestamps and clock reversals cannot skip captured rows.
  for (; next < end && next - cursor < Batch; ++next) {
    PageEvent event;
    if (input.history->read(next, event) != JournalResult::Ok) return unavailable();
    if ((event.flags & PageEvent::Gap) && (!event.open || event.open == open))
      return unavailable();  // Explicit coverage gap is never a fake successful upload.
    if ((event.flags & PageEvent::Gap) || event.open != open || event.start > captured || input.metadataAmbiguous)
      continue;
    batch_[count++] = {event.fraction, event.start, std::min<uint32_t>(event.seconds, 86400), event.scale};
    watermark = std::max(watermark, event.start);
  }
  if (!current(account, account_.snapshot())) return unavailable();
  if (count) {
    const auto reply = transport_.upload(token, {match.hash, match.bookFileId, batch_, count}, request.reconcile);
    if (!current(account, account_.snapshot()) || reply.result == StatsResult::Uncertain) {
      LOG_ERR("BSST", "Stats outcome uncertain; frozen batch retained for reconciliation");
      return PhaseOutcome::Uncertain;
    }
    if (reply.result != StatsResult::Ok || reply.hash != match.hash || reply.unmatched || reply.positionIncompatible ||
        reply.accepted > count || reply.duplicates != count - reply.accepted || reply.watermark != watermark)
      return unavailable();
  }
  memmove(output_ + HeaderBytes, matched.data(), matched.size());
  memcpy(output_, "BST1", 4);
  put(output_ + 4, next, 4);
  put(output_ + 8, begin, 4);
  put(output_ + 12, end, 4);
  put(output_ + 16, open, 8);
  put(output_ + 24, captured, 8);
  put(output_ + 32, matched.size(), 4);
  *request.confirmedCheckpoint = {output_, HeaderBytes + matched.size()};
  return next == end ? PhaseOutcome::Confirmed : PhaseOutcome::Checkpointed;
}
}  // namespace booksync::server
