// SPDX-License-Identifier: GPL-3.0-or-later
// Independently ports BookSync companion BookOrbitSync.kt's state semantics.
#include "LifecycleState.h"

#include <Logging.h>

#include <cstring>

namespace booksync::server {
namespace {
bool text(std::string_view value, size_t max) { return value.size() <= max && value.find('\0') == value.npos; }
bool valid(const StateValues& v) {
  return text(v.status, 32) && (!v.statusKnown || !v.status.empty()) && text(v.statusModified, 10) && v.rating <= 5 &&
         text(v.reviewNote, LifecycleState::MaxReviewBytes) && (v.reviewSet || v.reviewNote.empty()) &&
         (!v.reviewSet || v.reviewKnown);
}
bool current(const AccountSnapshot& before, const AccountSnapshot& after) {
  return after.syncReady && after.capabilities.sync && before.syncOwner == after.syncOwner &&
         before.ticket.owner == after.ticket.owner && before.ticket.epoch == after.ticket.epoch;
}
PhaseOutcome unavailable() {
  LOG_ERR("BSS", "State unavailable; pending capture and checkpoint retained");
  return PhaseOutcome::Unavailable;
}
PhaseOutcome uncertain() {
  LOG_ERR("BSS", "State outcome uncertain; reconcile before acknowledgement");
  return PhaseOutcome::Uncertain;
}
void put(char* p, size_t n) {
  p[0] = static_cast<char>(n);
  p[1] = static_cast<char>(n >> 8);
}
size_t get(const char* p) { return static_cast<uint8_t>(p[0]) | size_t(static_cast<uint8_t>(p[1])) << 8; }
}  // namespace
bool LifecycleState::decode(std::string_view bytes, std::string_view& matched, StateValues& state) {
  if (bytes.size() < HeaderBytes || bytes.size() > storage::LifecycleJournal::MaxCheckpoint ||
      bytes.substr(0, 4) != "BSS1" || (static_cast<uint8_t>(bytes[4]) & ~15U))
    return false;
  const auto matchSize = get(bytes.data() + 6), statusSize = get(bytes.data() + 8), dateSize = get(bytes.data() + 10),
             noteSize = get(bytes.data() + 12);
  if (HeaderBytes + matchSize + statusSize + dateSize + noteSize != bytes.size()) return false;
  const auto original = bytes.substr(HeaderBytes, matchSize);
  MatchCheckpoint identity;
  if (!LifecycleMatch::decode(original, identity)) return false;
  const auto flags = static_cast<uint8_t>(bytes[4]);
  StateValues value{bool(flags & 1),
                    bytes.substr(HeaderBytes + matchSize, statusSize),
                    bytes.substr(HeaderBytes + matchSize + statusSize, dateSize),
                    bool(flags & 2),
                    static_cast<uint8_t>(bytes[5]),
                    bool(flags & 4),
                    bool(flags & 8),
                    bytes.substr(HeaderBytes + matchSize + statusSize + dateSize, noteSize)};
  if (!valid(value)) return false;
  matched = original;
  state = value;
  return true;
}
PhaseOutcome LifecycleState::run(const SyncWriterToken& token, const PhaseRequest& request) {
  if (request.phase != storage::LifecyclePhase::State) return PhaseOutcome::Unavailable;
  const auto account = account_.snapshot();
  const auto& entry = request.entry;
  std::string_view matched = entry.checkpoint;
  StateValues state;
  const bool pending = entry.checkpoint.substr(0, 4) == "BSS1";
  if (pending && !decode(entry.checkpoint, matched, state)) return unavailable();
  MatchCheckpoint identity;
  // Input is a bounded collection of borrowed views, not a review-note copy.
  StateInput input;
  if (!account.syncReady || !account.capabilities.sync || !request.confirmedCheckpoint || !output_ ||
      entry.acknowledged != 7 || !LifecycleMatch::decode(matched, identity) || !evidence_.load(entry, input) ||
      input.syncOwner != account.syncOwner || input.scope.server != entry.adapter ||
      input.scope.account != entry.owner || input.source != identity.source || input.rating > 5 ||
      !text(input.status, 32) || !text(input.statusModified, 64) || !text(input.reviewModified, 64) ||
      !text(input.reviewNote, MaxReviewBytes) || !valid(input.known) ||
      (!input.reviewPresent && !input.reviewNote.empty()) || !current(account, account_.snapshot()))
    return unavailable();
  if (pending) {
    if (input.open) return unavailable();
    if (input.manualStatusPending || input.capturedToken != input.currentToken) state.statusKnown = false;
    if (!evidence_.apply(entry, input, state)) return unavailable();
    if (!current(account, account_.snapshot())) return uncertain();
    // Known history is durable through apply before retiring the pending reply.
    *request.confirmedCheckpoint = matched;
    return PhaseOutcome::Confirmed;
  }
  if (matched.size() + HeaderBytes > capacity_ ||
      matched.size() + HeaderBytes > storage::LifecycleJournal::MaxCheckpoint)
    return unavailable();
  StateQuery query{identity.hash,
                   input.manualStatusPending ? std::string_view{} : input.status,
                   input.manualStatusPending || input.status.empty() ? std::string_view{} : input.statusModified,
                   input.rating,
                   !input.rating && input.known.ratingKnown && input.known.rating != 0,
                   input.reviewPresent,
                   input.reviewNote,
                   input.reviewModified.empty() ? input.statusModified : input.reviewModified,
                   !input.reviewPresent && input.known.reviewKnown && input.known.reviewSet};
  const auto reply = transport_.exchange(token, query, request.reconcile);
  if (!current(account, account_.snapshot()) || reply.result == StateResult::Uncertain) return uncertain();
  if (reply.result != StateResult::Ok) return unavailable();
  state = reply.values;
  if (!state.statusKnown && !state.ratingKnown && !state.reviewKnown && state.status.empty() &&
      state.statusModified.empty() && state.rating == 0 && state.reviewNote.empty() && valid(state)) {
    *request.confirmedCheckpoint = matched;
    return PhaseOutcome::Confirmed;
  }
  const size_t size =
      HeaderBytes + matched.size() + state.status.size() + state.statusModified.size() + state.reviewNote.size();
  // Never silently truncate a remote field or acknowledge an unpersistable result.
  if (!valid(state) || size > capacity_ || size > storage::LifecycleJournal::MaxCheckpoint) return uncertain();
  std::memcpy(output_, "BSS1", 4);
  output_[4] = state.statusKnown | state.ratingKnown << 1 | state.reviewKnown << 2 | state.reviewSet << 3;
  output_[5] = state.rating;
  put(output_ + 6, matched.size());
  put(output_ + 8, state.status.size());
  put(output_ + 10, state.statusModified.size());
  put(output_ + 12, state.reviewNote.size());
  size_t offset = HeaderBytes;
  for (auto value : {matched, state.status, state.statusModified, state.reviewNote}) {
    if (!value.empty()) std::memcpy(output_ + offset, value.data(), value.size());
    offset += value.size();
  }
  *request.confirmedCheckpoint = {output_, size};
  return PhaseOutcome::Checkpointed;
}
}  // namespace booksync::server
