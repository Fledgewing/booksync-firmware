// SPDX-License-Identifier: GPL-3.0-or-later
#include "Finish.h"

#include <Logging.h>

#include <cstring>

#include "SyncOwner.h"

namespace booksync {
using namespace storage;
namespace {
constexpr size_t PayloadBytes = 37;
constexpr std::string_view Cancelled = "BSFC1", Superseded = "BSFS1";
uint64_t get(const char* p) {
  uint64_t n = 0;
  for (unsigned i = 0; i < 8; ++i) n |= uint64_t(uint8_t(p[i])) << (8 * i);
  return n;
}
void put(char* p, uint64_t n) {
  for (unsigned i = 0; i < 8; ++i) p[i] = char(n >> (8 * i));
}
bool valid(FinishValues v) {
  return unsigned(v.status) <= unsigned(ReadingStatus::Rereading) && v.rating <= 5 && (v.ratingKnown || !v.rating);
}
bool dayValid(std::string_view day) {
  if (day.empty()) return true;
  if (day.size() != 10 || day[4] != '-' || day[7] != '-') return false;
  for (unsigned i = 0; i < 10; ++i)
    if (i != 4 && i != 7 && (day[i] < '0' || day[i] > '9')) return false;
  return day.substr(5, 2) >= "01" && day.substr(5, 2) <= "12" && day.substr(8, 2) >= "01" && day.substr(8, 2) <= "31";
}
void encode(char* p, const FinishIntent& v) {
  std::memset(p, 0, PayloadBytes);
  std::memcpy(p, "BSFI", 4);
  p[4] = 1;
  put(p + 5, v.position);
  put(p + 13, v.undoOf);
  p[21] = char(v.before.status);
  p[22] = v.before.ratingKnown;
  p[23] = v.before.rating;
  p[24] = char(v.desired.status);
  p[25] = v.desired.ratingKnown;
  p[26] = v.desired.rating;
  if (!v.day.empty()) std::memcpy(p + 27, v.day.data(), 10);
}
JournalResult finishFailure(JournalResult r) {
  LOG_ERR("BSF", "Finish intent retained/blocked: %u", unsigned(r));
  return r;
}
}  // namespace
bool Finish::ready() const { return scratch_ && capacity_ >= SyncOwner::ScratchBytes; }
bool Finish::decode(std::string_view p, FinishIntent& out) {
  if (p.size() != PayloadBytes || !recognizes(p) || p[4] != 1 || uint8_t(p[22]) > 1 || uint8_t(p[25]) > 1) return false;
  FinishIntent v{get(p.data() + 5),
                 get(p.data() + 13),
                 {ReadingStatus(uint8_t(p[21])), bool(p[22]), uint8_t(p[23])},
                 {ReadingStatus(uint8_t(p[24])), bool(p[25]), uint8_t(p[26])},
                 p.substr(27, 10)};
  if (v.day == std::string_view("\0\0\0\0\0\0\0\0\0\0", 10)) v.day = {};
  if (!v.position || !valid(v.before) || !valid(v.desired) || !dayValid(v.day) ||
      (v.desired.status == ReadingStatus::Unknown && !v.desired.ratingKnown))
    return false;
  out = v;
  return true;
}
bool Finish::settled(const JournalEntry& e) {
  FinishIntent v;
  return decode(e.payload, v) && !e.attempting &&
         (e.acknowledged == LifecycleJournal::AllPhases || (e.checkpoint == Cancelled || e.checkpoint == Superseded));
}
JournalResult Finish::enqueue(uint64_t position, std::string_view adapter, std::string_view account,
                              std::string_view document, FinishValues before, FinishValues desired,
                              std::string_view day, uint64_t& id) {
  if (!ready() || !position || !valid(before) || !valid(desired) || !dayValid(day) ||
      (desired.status == ReadingStatus::Unknown && !desired.ratingKnown))
    return finishFailure(JournalResult::Invalid);
  if (journal_.verify() != JournalResult::Ok) return finishFailure(JournalResult::RecoveryRequired);
  JournalEntry source;
  const auto r = journal_.read(position, source, scratch_, capacity_);
  if (r != JournalResult::Ok) return finishFailure(r);
  if (source.adapter != adapter || source.owner != account || source.document != document ||
      recognizes(source.payload) || (source.acknowledged & 24) || source.attempting >= 4)
    return finishFailure(JournalResult::Invalid);
  char payload[PayloadBytes];
  encode(payload, {position, 0, before, desired, day});
  // Capture before State/Progress begins, so the owner can withhold normal
  // state exchange until final position delivery. Its checkpoint is transferred
  // only after outgoing delivery proof; a fetch cannot authorize finish.
  return journal_.enqueue({0, adapter, account, document, {payload, sizeof(payload)}, {}, 0}, id);
}
JournalResult Finish::undo(uint64_t id, std::string_view adapter, std::string_view account, uint64_t& undoId) {
  if (!ready() || journal_.verify() != JournalResult::Ok) return finishFailure(JournalResult::RecoveryRequired);
  JournalEntry original;
  auto r = journal_.read(id, original, scratch_, capacity_);
  if (r != JournalResult::Ok) return finishFailure(r);
  FinishIntent intent;
  if (original.adapter != adapter || original.owner != account || !decode(original.payload, intent) || intent.undoOf)
    return finishFailure(JournalResult::Invalid);
  if (original.checkpoint == Superseded) return finishFailure(JournalResult::Invalid);
  if (original.checkpoint == Cancelled) {
    undoId = id;
    return JournalResult::Ok;
  }
  // Keep the original views intact while scanning with a small copied identity.
  char document[LifecycleJournal::MaxIdentity];
  const auto length = original.document.size();
  std::memcpy(document, original.document.data(), length);
  uint64_t cursor = id;
  while ((r = journal_.next(adapter, account, cursor, cursor)) == JournalResult::Ok) {
    JournalEntry newer;
    if (journal_.read(cursor, newer, scratch_, capacity_) != JournalResult::Ok)
      return finishFailure(JournalResult::IoError);
    if (newer.document != std::string_view(document, length)) continue;
    FinishIntent other;
    if (!recognizes(newer.payload)) continue;
    if (!decode(newer.payload, other)) return finishFailure(JournalResult::RecoveryRequired);
    if (other.undoOf == id) {
      undoId = cursor;
      return JournalResult::Ok;
    }
    return finishFailure(JournalResult::Invalid);  // A newer local choice supersedes this Undo.
  }
  if (r != JournalResult::NotFound) return finishFailure(r);
  if (journal_.read(id, original, scratch_, capacity_) != JournalResult::Ok || !decode(original.payload, intent))
    return finishFailure(JournalResult::IoError);
  bool cancelSafe = false;
  if (!original.attempting && original.acknowledged == 0) {
    JournalEntry position;
    r = journal_.read(intent.position, position, scratch_, capacity_);
    if (r != JournalResult::Ok) return finishFailure(r);
    if (position.adapter != adapter || position.owner != account ||
        position.document != std::string_view(document, length))
      return finishFailure(JournalResult::Invalid);
    cancelSafe = !(position.acknowledged & 16) && position.attempting != 5;
    if (journal_.read(id, original, scratch_, capacity_) != JournalResult::Ok || !decode(original.payload, intent))
      return finishFailure(JournalResult::IoError);
  }
  if (cancelSafe) {
    r = journal_.checkpoint(id, Cancelled);  // One atomic cancellation; never partial ack cancellation.
    if (r == JournalResult::Ok) undoId = id;
    return r;
  }
  if ((intent.desired.status != ReadingStatus::Unknown && intent.before.status == ReadingStatus::Unknown) ||
      (intent.desired.ratingKnown && !intent.before.ratingKnown))
    return finishFailure(JournalResult::Invalid);
  char payload[PayloadBytes];
  const FinishValues restore{
      intent.desired.status == ReadingStatus::Unknown ? ReadingStatus::Unknown : intent.before.status,
      intent.desired.ratingKnown, intent.desired.ratingKnown ? intent.before.rating : uint8_t(0)};
  encode(payload, {intent.position, id, intent.desired, restore, {}});
  // Original receipt remains until compensation settles, including an unknown write.
  return journal_.enqueue({0, adapter, account, original.document, {payload, sizeof(payload)}, original.checkpoint, 1},
                          undoId);
}
JournalResult Finish::positionPlan(const JournalEntry& source, bool& suppressState, bool& cancelPosition) {
  suppressState = false;
  cancelPosition = false;
  bool active = false;
  if (!ready()) return finishFailure(JournalResult::Invalid);
  uint64_t cursor = 0;
  JournalResult r;
  while ((r = journal_.next(source.adapter, source.owner, cursor, cursor)) == JournalResult::Ok) {
    JournalEntry e;
    if (journal_.read(cursor, e, scratch_, capacity_) != JournalResult::Ok)
      return finishFailure(JournalResult::IoError);
    if (!recognizes(e.payload)) continue;
    FinishIntent v;
    if (!decode(e.payload, v)) return finishFailure(JournalResult::RecoveryRequired);
    if (!v.undoOf && v.position == source.id) {
      if (e.document != source.document) return finishFailure(JournalResult::Invalid);
      suppressState = true;
      if (e.checkpoint == Cancelled)
        cancelPosition = true;
      else
        active = true;
    }
  }
  cancelPosition = cancelPosition && !active;
  return r == JournalResult::NotFound ? JournalResult::Ok : finishFailure(r);
}
JournalResult Finish::positionDelivered(const JournalEntry& source) {
  if (!ready() || source.acknowledged != LifecycleJournal::AllPhases || source.attempting)
    return finishFailure(JournalResult::Invalid);
  uint64_t cursor = 0;
  JournalResult r;
  while ((r = journal_.next(source.adapter, source.owner, cursor, cursor)) == JournalResult::Ok) {
    JournalEntry e;
    if (journal_.read(cursor, e, scratch_, capacity_) != JournalResult::Ok)
      return finishFailure(JournalResult::IoError);
    if (!recognizes(e.payload)) continue;
    FinishIntent v;
    if (!decode(e.payload, v)) return finishFailure(JournalResult::RecoveryRequired);
    if (v.undoOf || v.position != source.id || settled(e)) continue;
    if (!proof_.delivered(source)) return finishFailure(JournalResult::Incomplete);
    if (e.document != source.document || e.attempting || e.acknowledged > 1)
      return finishFailure(JournalResult::Invalid);
    if (!e.acknowledged && journal_.acknowledge(e.id, LifecyclePhase::Match, source.checkpoint) != JournalResult::Ok)
      return finishFailure(JournalResult::IoError);
  }
  return r == JournalResult::NotFound ? JournalResult::Ok : finishFailure(r);
}
SyncResult Finish::step(const SyncWriterToken& token, ServerAdapter& adapter, const JournalEntry& e) {
  FinishIntent v;
  if (!ready() || !decode(e.payload, v) || !e.acknowledged) return SyncResult::Unavailable;
  if (!v.undoOf && !e.attempting && e.acknowledged == 1) {
    uint64_t cursor = e.id;
    JournalResult r;
    while ((r = journal_.next(e.adapter, e.owner, cursor, cursor)) == JournalResult::Ok) {
      JournalEntry newer;
      if (journal_.read(cursor, newer, scratch_, capacity_) != JournalResult::Ok) return SyncResult::StorageError;
      if (newer.document != e.document || !recognizes(newer.payload)) continue;
      FinishIntent choice;
      if (!decode(newer.payload, choice)) return SyncResult::StorageError;
      if (!choice.undoOf && newer.checkpoint != Cancelled && newer.checkpoint != Superseded) {
        return journal_.checkpoint(e.id, Superseded) == JournalResult::Ok ? SyncResult::Advanced
                                                                          : SyncResult::StorageError;
      }
    }
    if (r != JournalResult::NotFound) return SyncResult::StorageError;
  }
  if (v.undoOf) {
    JournalEntry original;
    if (journal_.read(v.undoOf, original, scratch_, capacity_) != JournalResult::Ok || original.adapter != e.adapter ||
        original.owner != e.owner || original.document != e.document || !settled(original))
      return SyncResult::Unavailable;
    if (e.checkpoint.empty()) {
      if (journal_.checkpoint(e.id, original.checkpoint) != JournalResult::Ok) return SyncResult::StorageError;
      return SyncResult::Advanced;
    }
  }
  unsigned phase = 0;
  while (phase < 5 && (e.acknowledged & (1U << phase))) ++phase;
  if (phase >= 5 || e.acknowledged != (1U << phase) - 1 || (e.attempting && e.attempting != phase + 1))
    return SyncResult::StorageError;
  const auto selected = LifecyclePhase(phase);
  const bool skip = (phase == 1 && (v.day.empty() || v.desired.status == ReadingStatus::Unknown)) ||
                    (phase == 2 && v.desired.status == ReadingStatus::Unknown) ||
                    (phase == 3 && !v.desired.ratingKnown);
  if (skip) {
    if (e.attempting) return SyncResult::StorageError;
    return journal_.acknowledge(e.id, selected, e.checkpoint) == JournalResult::Ok ? SyncResult::Advanced
                                                                                   : SyncResult::StorageError;
  }
  if (!e.attempting && journal_.attempt(e.id, selected) != JournalResult::Ok) return SyncResult::StorageError;
  auto checkpoint = e.checkpoint;
  const auto outcome = adapter.finishPhase(token, {e, v, selected, e.attempting != 0, &checkpoint});
  if (outcome == PhaseOutcome::Uncertain) return SyncResult::Uncertain;
  if (outcome == PhaseOutcome::Checkpointed) {
    if (checkpoint == e.checkpoint || journal_.checkpoint(e.id, checkpoint) != JournalResult::Ok)
      return SyncResult::StorageError;
    return SyncResult::Advanced;
  }
  if (outcome != PhaseOutcome::Confirmed) return SyncResult::Unavailable;
  if (journal_.acknowledge(e.id, selected, checkpoint) != JournalResult::Ok) return SyncResult::StorageError;
  return SyncResult::Advanced;
}
}  // namespace booksync

// Dormant service remains in every real-image reserve measurement before UI activation.
extern "C" {
struct FinishLinkAnchor {
  decltype(&booksync::Finish::enqueue) enqueue;
  decltype(&booksync::Finish::undo) undo;
};
extern const FinishLinkAnchor booksync_finish_link_anchor = {&booksync::Finish::enqueue, &booksync::Finish::undo};
}
