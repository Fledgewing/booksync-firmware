// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeCaptureGate.h"

#include <Logging.h>

namespace booksync::server {
bool NativeCaptureGate::current() const {
  const auto now = account_.snapshot();
  return session_.syncReady && session_.capabilities.sync && now.syncReady && now.capabilities.sync &&
         now.ticket.owner == session_.ticket.owner && now.ticket.epoch == session_.ticket.epoch &&
         now.syncOwner == session_.syncOwner;
}
bool NativeCaptureGate::verify(const storage::JournalEntry& entry) {
  pendingProgress_ = false;
  pendingState_ = false;
  if (!entry.id || !current() || entry.adapter != scope_.server || entry.owner != scope_.account ||
      !reader::Capture::decodeOpen(entry.payload, opened_) || !opened_.bound || opened_.binding.scope != scope_)
    return false;
  auto native = entry.checkpoint;
  if (native.substr(0, 4) == "BSP1") {
    std::string_view matched;
    if (!LifecycleProgress::decode(native, matched, progress_)) return false;
    pendingProgress_ = true;
    native = matched;
  } else if (native.substr(0, 4) == "BSS1") {
    std::string_view matched;
    if (!LifecycleState::decode(native, matched, state_)) return false;
    pendingState_ = true;
    native = matched;
  }
  if (native.substr(0, 4) != "BSC2") {
    if (!LifecycleMatch::decode(native, match_) || match_.source != opened_.binding.source) return false;
    native = match_.native;
  }
  bool closed = false;
  if (native.substr(0, 4) != "BSC2" || !reader::Capture::decode(native, sample_, closed) || !closed ||
      !sample_.nativeOpen || !file_.verify(opened_.binding.source, opened_.path) || !current()) {
    LOG_ERR("BSE", "Native capture no longer has closed source/account proof");
    return false;
  }
  return true;
}
}  // namespace booksync::server
