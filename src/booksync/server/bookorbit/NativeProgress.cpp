// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeProgress.h"

#include <Logging.h>

namespace booksync::server {
bool NativeProgress::protocol(const storage::PositionValue& value) const {
  return value.present && value.percentagePresent && value.kind == ProtocolLocator && value.version == 1 && value.at &&
         !value.raw.empty() && value.raw.find('\0') == value.raw.npos;
}
bool NativeProgress::freeze(const storage::JournalEntry& entry, const storage::PositionValue& value, bool deliver,
                            bool wasOverride) {
  loaded_ = 0;
  if (entry.acknowledged || entry.attempting || entry.checkpoint.substr(0, 4) != "BSC2" ||
      (deliver && !protocol(value)) || !gate_.verify(entry)) {
    LOG_ERR("BSP", "Progress cannot recapture an attempted or unsupported intent");
    return false;
  }
  const auto& opened = gate_.opened();
  frozen_ = {scope_,  opened.binding.source, entry.id,      gate_.sample().nativeOpen, entry.payload, value,
             deliver, wasOverride,           entry.document};
  return capture_.freeze(frozen_);
}
bool NativeProgress::restoreProjection() {
  if (!scratch_ || capacity_ < storage::PositionStore::MaxBytes || !capture_.disjoint(scratch_, capacity_)) {
    LOG_ERR("BSP", "Progress projection workspace unavailable or overlapping capture");
    return false;
  }
  const auto result = projection_.load(scope_, frozen_.source, current_, scratch_, capacity_);
  if (result == storage::PositionLoad::Ready) return true;
  if (result != storage::PositionLoad::Missing) {
    LOG_ERR("BSP", "Progress projection unavailable; no replacement");
    return false;
  }
  current_ = {};
  current_.scope = scope_;
  current_.source = frozen_.source;
  return true;
}
bool NativeProgress::load(const storage::JournalEntry& entry, ProgressInput& output) {
  loaded_ = 0;
  if (!gate_.verify(entry) || !capture_.load(scope_, gate_.opened().binding.source, entry.id, gate_.sample().nativeOpen,
                                             entry.payload, entry.document, frozen_))
    return false;
  if (frozen_.deliver) {
    if (!protocol(frozen_.value) || !restoreProjection()) return false;
    PositionSync policy(projection_, current_);
    // A BSP1 pending result already passed transport and must never send again.
    // Current permission gates only a new outgoing request, not local recovery.
    if (!gate_.pendingProgress() && (current_.overridePlace.present != frozen_.wasOverride ||
                                     (frozen_.wasOverride && current_.overridePlace != frozen_.value) ||
                                     !policy.mayDeliver(frozen_.value.percentage, frozen_.value.at)))
      return false;
  }
  loaded_ = entry.id;
  output = {scope_,
            gate_.owner(),
            frozen_.source,
            frozen_.deliver,
            frozen_.value.percentage,
            protocol(frozen_.value) ? frozen_.value.raw : std::string_view{},
            frozen_.value.at};
  return true;
}
bool NativeProgress::apply(const storage::JournalEntry& entry, const ProgressInput& input,
                           const ProgressValues& reply) {
  if (!loaded_ || entry.id != loaded_ || entry.document != frozen_.document || input.scope != scope_ ||
      input.source != frozen_.source || input.syncOwner != gate_.owner() || input.deliver != frozen_.deliver ||
      input.percentage != frozen_.value.percentage || input.capturedAt != frozen_.value.at ||
      input.position != (protocol(frozen_.value) ? frozen_.value.raw : std::string_view{}) || !gate_.verify(entry) ||
      gate_.opened().binding.source != frozen_.source || !restoreProjection())
    return false;
  PositionSync policy(projection_, current_);
  if (frozen_.deliver) {
    // Only a confirmed F18e result reaches this seam. Comparing the LOCAL
    // publication makes replay idempotent; it is never remote commit evidence.
    const bool already =
        !current_.overridePlace.present && current_.remote == frozen_.value && current_.marker == frozen_.value;
    if (!already && (current_.overridePlace.present != frozen_.wasOverride ||
                     (frozen_.wasOverride && current_.overridePlace != frozen_.value) ||
                     !policy.mayDeliver(frozen_.value.percentage, frozen_.value.at)))
      return false;
    return policy.sent(frozen_.value, frozen_.wasOverride) == PositionResult::Saved;
  }
  storage::PositionValue remote{};
  remote.present = reply.present;
  remote.percentagePresent = reply.percentagePresent;
  remote.percentage = reply.percentage;
  remote.at = reply.timestamp;
  remote.kind = reply.present ? ProtocolLocator : 0;
  remote.version = reply.present ? 1 : 0;
  remote.raw = reply.position;
  remote.label = reply.label;
  remote.reset = reply.reset;
  remote.browser = reply.web;
  // Exact raw locator equality is lossless same-place evidence. More complex
  // native geometry comparisons remain with the source mapper/policy owner.
  const auto& marker = current_.marker;
  remote.redated = remote.present && marker.present && !remote.raw.empty() && marker.kind == remote.kind &&
                   marker.version == remote.version && marker.raw == remote.raw && marker.at != remote.at &&
                   marker.percentagePresent == remote.percentagePresent && marker.percentage == remote.percentage;
  return policy.observe(scope_, frozen_.source, remote) == PositionResult::Saved;
}
}  // namespace booksync::server
