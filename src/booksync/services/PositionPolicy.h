// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <optional>

#include "booksync/storage/PositionStore.h"

namespace booksync {
struct PositionMarker {
  uint64_t at;
  double place;
};
struct PositionInput {
  double local = 0;
  std::optional<double> remote;
  double tolerance = 1;
  uint64_t remoteAt = 0;
  std::optional<PositionMarker> marker;
  uint64_t turnAt = 0;
  bool reset = false, redated = false, finished = false, released = false;
  uint64_t declinedAt = 0;
  std::optional<uint64_t> overrideAt;
  bool percentOnly = false;
};
enum class PositionAction { Same, Take, Follow, Push, Hold, Stay, Override };
enum class PositionApply { No, Now, Ask, Wake };
struct PositionDecision {
  PositionAction action;
  const char* rule;
  bool dropped = false;
};
enum class PositionStrategy { Keep, Ask, Follow };
struct PositionMode {
  bool automatic = true, compared = false;
  PositionStrategy ahead = PositionStrategy::Ask, behind = PositionStrategy::Keep;
};
class PositionPolicy {
 public:
  static constexpr uint64_t SkewSeconds = 600;
  static PositionDecision decide(const PositionInput&);
  static PositionDecision decide(const PositionInput&, PositionMode);
  static PositionApply apply(PositionAction, std::string_view moment);
  static PositionApply apply(PositionDecision, std::string_view moment, PositionMode);
  static bool mayPush(std::optional<double> place, std::optional<double> remote, double tolerance = 1,
                      bool exempt = false);
};
enum class PositionResult { Saved, Stale, Conflict, Invalid, StorageError };
// Worker-only durable policy seam for F18e ProgressEvidence. No transport, task,
// UI callback or native movement. Load the record before construction. Any save
// failure requires reload/reconciliation before further use. Every operation
// borrows the record's text/raw inputs. Reload from PositionStore before a reply
// or capture workspace is reused; never publish these private views to UI.
// Callers recheck F15 account epoch and F12 document/path/run/mode at delivery.
class PositionSync {
 public:
  PositionSync(storage::PositionStore& store, storage::PositionRecord& record) : store_(store), record_(record) {}
  PositionResult observe(storage::ScopeView, const storage::SourceIdentity&, const storage::PositionValue&);
  PositionResult opened();  // Reset Custom first-comparison state for a new native open.
  PositionResult overrideHere(const storage::PositionValue&);
  PositionResult evaluate(uint64_t token, PositionInput geometry, std::string_view moment, PositionDecision&,
                          PositionApply&, PositionMode mode = {});
  // A Saved Yes is the only answer that authorizes a native move. Duplicate and
  // stale answers never move; caller must also pass the current F12 lifetime.
  PositionResult answer(uint64_t token, bool yes);
  // Custom directions exempt the behind-hold. Only close/sleep/suspend Custom
  // captures may bypass a saved hold/decline; ordinary decisions obey allowPush.
  bool mayDeliver(double capturedPercentage, uint64_t capturedAt, bool custom = false, bool lifecycle = false) const;
  PositionResult sent(const storage::PositionValue&, bool wasOverride);

 private:
  storage::PositionStore& store_;
  storage::PositionRecord& record_;
  bool ready_ = true;
  PositionResult persist();
};
}  // namespace booksync
