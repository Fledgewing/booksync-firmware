// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/Reminders.h"
namespace booksync {
// A failed durable claim cannot spin SD retries at the input-loop rate. Keep the
// occurrence service-owned; retry at most minutely, or in a new visible lifetime.
class ReminderClaimRetry {
 public:
  bool allows(const ReminderDue& due, uint64_t elapsed) const {
    return due.id != id_ || due.revision != revision_ || due.kind != kind_ || due.source != source_ ||
           due.day != day_ || due.session != session_ || due.sequence != sequence_ || elapsed >= until_;
  }
  void failed(ReminderStatus result, const ReminderDue& due, uint64_t elapsed) {
    if (result != ReminderStatus::StorageError && result != ReminderStatus::Unavailable) return;
    id_ = due.id;
    revision_ = due.revision;
    kind_ = due.kind;
    source_ = due.source;
    day_ = due.day;
    session_ = due.session;
    sequence_ = due.sequence;
    until_ = elapsed > UINT64_MAX - 60000 ? UINT64_MAX : elapsed + 60000;
  }
  void reset() { id_ = 0; }

 private:
  uint64_t id_ = 0, revision_ = 0, session_ = 0, sequence_ = 0, until_ = 0;
  int32_t day_ = 0;
  ReminderKind kind_ = ReminderKind::Time;
  ReminderSource source_ = ReminderSource::Local;
};
static_assert(sizeof(ReminderClaimRetry) <= 64);
}  // namespace booksync
