// SPDX-License-Identifier: GPL-3.0-or-later
// Independent C++ implementation of BookSync companion PositionPolicy.kt and
// SyncOwner.kt policy semantics; shared recorded cases are the acceptance source.
#include "PositionPolicy.h"

#include <Logging.h>

#include <cmath>
#include <limits>

namespace booksync {
PositionDecision PositionPolicy::decide(const PositionInput& i) {
  const bool moved = i.marker && i.remote && i.remoteAt != i.marker->at && !i.redated;
  const bool dropped = i.overrideAt && moved && i.remoteAt > *i.overrideAt;
  auto result = [dropped](PositionAction a, const char* rule) { return PositionDecision{a, rule, dropped}; };
  auto move = [&](PositionAction a, const char* rule) {
    return i.declinedAt && i.declinedAt == i.remoteAt ? result(PositionAction::Stay, "declined") : result(a, rule);
  };
  if (i.overrideAt && !dropped) return result(PositionAction::Override, "override");
  if (!i.remote) return result(PositionAction::Push, "no-server");
  if (std::abs(i.local - *i.remote) <= i.tolerance) return result(PositionAction::Same, "rule1");
  if (i.reset) return move(PositionAction::Follow, "rule2");
  if (i.finished && i.local < *i.remote)
    return result(i.released ? PositionAction::Push : PositionAction::Hold, "rule7");
  if (*i.remote > i.local) return move(PositionAction::Take, "rule3");
  if (!moved) return result(PositionAction::Push, "rule4");
  if (i.percentOnly) return result(PositionAction::Push, "percent");
  if (std::abs(i.local - i.marker->place) <= i.tolerance) return move(PositionAction::Follow, "rule5");
  if (i.remoteAt > i.turnAt && i.remoteAt - i.turnAt > SkewSeconds) return move(PositionAction::Follow, "rule6");
  return result(PositionAction::Push, "rule6");
}
PositionDecision PositionPolicy::decide(const PositionInput& i, PositionMode mode) {
  if (mode.automatic || i.overrideAt) return decide(i);
  if (!i.remote) return {PositionAction::Push, "custom-empty"};
  if (std::abs(i.local - *i.remote) <= i.tolerance) return {PositionAction::Same, "custom-same"};
  const bool newer = i.turnAt ? i.remoteAt > i.turnAt || (*i.remote > i.local && !mode.compared) : *i.remote > i.local;
  const auto strategy = newer ? mode.ahead : mode.behind;
  if (strategy == PositionStrategy::Keep) return {newer ? PositionAction::Hold : PositionAction::Push, "custom-keep"};
  if (i.declinedAt && i.declinedAt == i.remoteAt)
    return {newer ? PositionAction::Stay : PositionAction::Push, "custom-declined"};
  return {newer ? PositionAction::Take : PositionAction::Follow,
          strategy == PositionStrategy::Ask ? "custom-ask" : "custom-follow"};
}
PositionApply PositionPolicy::apply(PositionAction action, std::string_view moment) {
  if (action != PositionAction::Take && action != PositionAction::Follow) return PositionApply::No;
  if (moment == "prerender") return PositionApply::Now;
  if (moment == "sleep") return PositionApply::Wake;
  if (moment == "close") return PositionApply::No;
  return PositionApply::Ask;
}
PositionApply PositionPolicy::apply(PositionDecision d, std::string_view moment, PositionMode mode) {
  const std::string_view rule(d.rule);
  if (!mode.automatic && rule.substr(0, 7) == "custom-") {
    if (moment == "prerender" && rule != "custom-follow") return PositionApply::No;
    if (moment == "pause" && rule == "custom-ask") return PositionApply::No;
  }
  return apply(d.action, moment);
}
bool PositionPolicy::mayPush(std::optional<double> place, std::optional<double> remote, double tolerance, bool exempt) {
  return exempt || !remote || !place || *place >= *remote - tolerance;
}
PositionResult PositionSync::persist() {
  if (!ready_) return PositionResult::StorageError;
  if (store_.save(record_)) return PositionResult::Saved;
  ready_ = false;
  LOG_ERR("BSP", "Position policy not durably saved; reload before continuing");
  return PositionResult::StorageError;
}
PositionResult PositionSync::observe(storage::ScopeView scope, const storage::SourceIdentity& source,
                                     const storage::PositionValue& remote) {
  if (!ready_) return PositionResult::StorageError;
  if (scope != record_.scope || source != record_.source) return PositionResult::Conflict;
  // Equal bytes may be visible after a previous final directory barrier failed.
  // Re-publication confirms durability before the owner can acknowledge them.
  if (record_.token && remote == record_.remote) {
    if (!remote.reset) record_.declinedReset = false;
    return persist();
  }
  if (record_.token == std::numeric_limits<uint64_t>::max()) return PositionResult::Invalid;
  ++record_.token;
  record_.remote = remote;
  record_.allowPush = record_.answerable = false;
  // A refresh invalidates the old answer, not a saved wake-local reminder.
  // The newly observed place still needs its own decision before presentation.
  if (!remote.present) record_.pending = false;
  if (!remote.reset) record_.declinedReset = false;
  return persist();
}
PositionResult PositionSync::opened() {
  if (!ready_) return PositionResult::StorageError;
  record_.compared = false;
  return persist();
}
PositionResult PositionSync::overrideHere(const storage::PositionValue& value) {
  if (!ready_) return PositionResult::StorageError;
  if (!value.present || !value.at) return PositionResult::Invalid;
  record_.overridePlace = value;
  record_.declinedAt = 0;
  record_.declinedReset = record_.pending = record_.answerable = record_.allowPush = false;
  return persist();
}
PositionResult PositionSync::evaluate(uint64_t token, PositionInput geometry, std::string_view moment,
                                      PositionDecision& decision, PositionApply& applied, PositionMode mode) {
  if (!ready_) return PositionResult::StorageError;
  if (!token || token != record_.token) return PositionResult::Stale;
  if (!std::isfinite(geometry.local) || geometry.local < 0 || !std::isfinite(geometry.tolerance) ||
      geometry.tolerance < 0 || (geometry.remote && (!std::isfinite(*geometry.remote) || *geometry.remote < 0)) ||
      (geometry.marker && (!std::isfinite(geometry.marker->place) || geometry.marker->place < 0)))
    return PositionResult::Invalid;
  geometry.remoteAt = record_.remote.at;
  geometry.reset = record_.remote.reset;
  geometry.redated = record_.remote.redated;
  if (!record_.remote.present) geometry.remote.reset();
  if (!record_.marker.present)
    geometry.marker.reset();
  else if (geometry.marker)
    geometry.marker->at = record_.marker.at;
  geometry.declinedAt = record_.remote.reset && record_.declinedReset ? record_.remote.at : record_.declinedAt;
  geometry.overrideAt =
      record_.overridePlace.present ? std::optional<uint64_t>(record_.overridePlace.at) : std::nullopt;
  mode.compared = record_.compared;
  record_.compared = true;
  const auto d = PositionPolicy::decide(geometry, mode);
  const auto a = PositionPolicy::apply(d, moment == "wake-local" ? "wake" : moment, mode);
  if (d.dropped) record_.overridePlace = {};
  if (d.action == PositionAction::Same) {
    record_.marker = record_.remote;
    record_.pending = false;
  }
  if (std::string_view(d.rule) == "rule7" && d.action == PositionAction::Push) {
    record_.rereadAt = record_.remote.at;
    record_.rereadReleased = true;
  }
  if (a == PositionApply::Wake) record_.pending = true;
  record_.answerable = a != PositionApply::No;
  record_.allowPush = d.action == PositionAction::Push || d.action == PositionAction::Override;
  const auto status = persist();
  if (status == PositionResult::Saved) {
    decision = d;
    applied = a;
  }
  return status;
}
PositionResult PositionSync::answer(uint64_t token, bool yes) {
  if (!ready_) return PositionResult::StorageError;
  if (!token || token != record_.token || !record_.answerable || !record_.remote.present) return PositionResult::Stale;
  record_.allowPush = record_.answerable = record_.pending = false;
  if (yes)
    record_.marker = record_.remote;
  else {
    record_.declinedAt = record_.remote.at;
    record_.declinedReset = record_.remote.reset;
  }
  return persist();
}
bool PositionSync::mayDeliver(double percentage, uint64_t capturedAt, bool custom, bool lifecycle) const {
  if (!ready_) return false;
  if (!std::isfinite(percentage) || percentage < 0 || percentage > 1 || capturedAt < record_.lastOverrideAt)
    return false;
  const bool exempt =
      record_.overridePlace.present || (record_.rereadReleased && record_.rereadAt == record_.remote.at);
  const auto remote =
      record_.remote.percentagePresent ? std::optional<double>(record_.remote.percentage * 100) : std::nullopt;
  return (record_.allowPush || (custom && lifecycle)) &&
         PositionPolicy::mayPush(percentage * 100, remote, 1, exempt || custom);
}
PositionResult PositionSync::sent(const storage::PositionValue& committed, bool wasOverride) {
  if (!ready_) return PositionResult::StorageError;
  if (!committed.present || !committed.at) return PositionResult::Invalid;
  // Replaying F18e publication after a cut between this save and its journal
  // acknowledgement must not mint another token or lose the override watermark.
  if (!record_.overridePlace.present && record_.remote == committed && record_.marker == committed) return persist();
  if (wasOverride && !record_.overridePlace.present) return PositionResult::Invalid;
  if (record_.token == std::numeric_limits<uint64_t>::max()) return PositionResult::Invalid;
  if (wasOverride) record_.lastOverrideAt = record_.overridePlace.at;
  record_.remote = record_.marker = committed;
  record_.overridePlace = {};
  record_.rereadAt = 0;
  record_.rereadReleased = false;
  record_.pending = record_.answerable = record_.allowPush = false;
  ++record_.token;
  return persist();
}
}  // namespace booksync

// Budget root only, never invoked by runtime. Keep the complete dormant policy
// and store in real image/map measurements before live evidence is connected.
extern "C" bool booksync_position_link(booksync::PositionSync& sync, booksync::storage::PositionStore& store,
                                       booksync::storage::PositionRecord& record, char* scratch, size_t capacity,
                                       booksync::PositionInput geometry, booksync::PositionMode mode,
                                       std::string_view moment, bool yes) {
  const auto loaded = store.load(record.scope, record.source, record, scratch, capacity);
  const auto observed = sync.observe(record.scope, record.source, record.remote);
  const auto opened = sync.opened();
  const auto overridden = sync.overrideHere(record.overridePlace);
  booksync::PositionDecision decision{};
  booksync::PositionApply applied{};
  const auto evaluated = sync.evaluate(record.token, geometry, moment, decision, applied, mode);
  const auto answer = sync.answer(record.token, yes);
  const bool push = sync.mayDeliver(record.remote.percentage, record.remote.at, !mode.automatic);
  const auto sent = sync.sent(record.remote, yes);
  return loaded == booksync::storage::PositionLoad::Ready && opened == observed && observed == overridden &&
         evaluated == answer && answer == sent && push;
}
