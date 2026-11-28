// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeState.h"

#include <Logging.h>

#include <cinttypes>
#include <cstdio>

namespace booksync::server {
bool NativeState::restoreCurrent(const storage::SourceIdentity& source) {
  if (!capture_.disjoint(store_)) {
    LOG_ERR("BSS", "Native state workspaces overlap");
    return false;
  }
  const auto result = store_.load(scope_, source, current_);
  if (result == NativeStateLoad::Ready || result == NativeStateLoad::Missing) return true;
  LOG_ERR("BSS", "Native state source unavailable; no replacement");
  return false;
}
bool NativeState::freeze(const storage::JournalEntry& entry) {
  loaded_ = 0;
  if (entry.acknowledged || entry.attempting || entry.checkpoint.substr(0, 4) != "BSC2" || !gate_.verify(entry) ||
      !restoreCurrent(gate_.opened().binding.source))
    return false;
  frozen_ = current_;
  frozen_.entry = entry.id;
  frozen_.nativeOpen = gate_.sample().nativeOpen;
  frozen_.payload = entry.payload;
  frozen_.document = entry.document;
  return capture_.freeze(frozen_);
}
bool NativeState::load(const storage::JournalEntry& entry, StateInput& output) {
  loaded_ = 0;
  if (!capture_.disjoint(store_) || !gate_.verify(entry) ||
      !capture_.loadCapture(scope_, gate_.opened().binding.source, entry.id, gate_.sample().nativeOpen, entry.payload,
                            entry.document, frozen_) ||
      !restoreCurrent(frozen_.source))
    return false;
  std::snprintf(capturedToken_.data(), capturedToken_.size(), "%016" PRIx64, frozen_.token);
  std::snprintf(currentToken_.data(), currentToken_.size(), "%016" PRIx64, current_.token);
  const auto& native = frozen_.local;
  const bool review = native.reviewKnown && native.reviewSet;
  output = {scope_,
            gate_.owner(),
            frozen_.source,
            native.statusKnown ? native.status : std::string_view{},
            native.statusModified,
            native.ratingKnown ? native.rating : uint8_t(0),
            review,
            review ? native.reviewNote : std::string_view{},
            frozen_.reviewModified,
            frozen_.history,
            frozen_.manualStatusPending || current_.manualStatusPending,
            false,
            capturedToken_.data(),
            currentToken_.data()};
  loaded_ = entry.id;
  return true;
}
bool NativeState::apply(const storage::JournalEntry& entry, const StateInput& input, const StateValues& values) {
  if (!loaded_ || loaded_ != entry.id || entry.document != frozen_.document || input.scope != scope_ ||
      input.source != frozen_.source || input.syncOwner != gate_.owner() ||
      input.capturedToken != capturedToken_.data() || input.currentToken != currentToken_.data() ||
      !gate_.verify(entry) || gate_.opened().binding.source != frozen_.source || !restoreCurrent(frozen_.source))
    return false;
  // F18d may mask statusKnown after a newer manual intent. Preserve the original
  // staged remote knowledge separately; masking is local-write permission only.
  const auto& wire = gate_.pendingState() ? gate_.stateReply() : values;
  current_.history = wire;
  if (values.statusKnown && wire.statusKnown && !frozen_.manualStatusPending && !current_.manualStatusPending &&
      current_.token == frozen_.token) {
    current_.local.statusKnown = true;
    current_.local.status = wire.status;
    current_.local.statusModified = wire.statusModified;
  }
  if (wire.ratingKnown) {
    current_.local.ratingKnown = true;
    current_.local.rating = wire.rating;
  }
  if (wire.reviewKnown) {
    current_.local.reviewKnown = true;
    current_.local.reviewSet = wire.reviewSet;
    current_.local.reviewNote = wire.reviewNote;
    current_.reviewModified = wire.statusModified;  // Empty remains unknown; no invented time.
  }
  // Background publication does not mint a native user-status version. A cut
  // before phase ack replays the same state; only explicit local intents advance
  // the native token. Pending manual intent and unrelated native fields remain.
  return store_.save(current_);
}
}  // namespace booksync::server
