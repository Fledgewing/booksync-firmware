// SPDX-License-Identifier: GPL-3.0-or-later
#include "SeriesQueue.h"

#include <Logging.h>

#include <cstdio>
#include <cstring>
#include <limits>
namespace booksync {
namespace {
bool queueTerminal(SeriesItemState state) {
  return state == SeriesItemState::Complete || state == SeriesItemState::Canceled ||
         state == SeriesItemState::Unsupported;
}
bool queueTicket(DeliveryTicket a, DeliveryTicket b) { return a.owner == b.owner && a.epoch == b.epoch; }
}  // namespace
SeriesQueue::SeriesQueue(storage::SeriesQueueStore& store, DownloadJobs& engine, DownloadSource& source,
                         SeriesQueuePorts ports, storage::ScopeView scope, DeliveryTicket owner, const char* prefix)
    : store_(store), engine_(engine), source_(source), ports_(ports), owner_(owner) {
  if (!owner.owner || !owner.epoch || !prefix || !*prefix || std::strlen(prefix) >= prefix_.size() ||
      scope.server.empty() || scope.account.empty() || scope.server.size() >= server_.size() ||
      scope.account.size() >= account_.size() || scope.server.find('\0') != scope.server.npos ||
      scope.account.find('\0') != scope.account.npos)
    return;
  std::memcpy(server_.data(), scope.server.data(), scope.server.size());
  std::memcpy(account_.data(), scope.account.data(), scope.account.size());
  std::strcpy(prefix_.data(), prefix);
  valid_ = ports.writable && ports.online && ports.reviewCurrent;
}
bool SeriesQueue::allowed() const { return valid_ && ports_.writable(ports_.context); }
bool SeriesQueue::connected() const { return valid_ && ports_.online(ports_.context); }
int SeriesQueue::locate(uint64_t id) const {
  if (id)
    for (unsigned i = 0; i < 4; ++i)
      if (index_.slots[i].id == id) return i;
  return -1;
}
uint64_t SeriesQueue::find(const char* series) const {
  if (!ready_ || !series) return 0;
  uint64_t newest = 0;
  for (const auto& slot : index_.slots)
    if (slot.id > newest && std::strcmp(slot.series.data(), series) == 0) newest = slot.id;
  return newest;
}
bool SeriesQueue::recover() {
  if (!allowed() || engine_.busy()) return false;
  ready_ = store_.recover(index_);
  requested_.fill(false);
  attempted_.fill(0);
  slot_ = 4;
  row_ = SeriesNextMax;
  dirty_ = indexDirty_ = false;
  if (!ready_) LOG_ERR("BSQ", "Series queue registry recovery failed");
  return ready_;
}
bool SeriesQueue::checkpoint() {
  if (!allowed() || engine_.busy()) return false;
  if (dirty_) {
    if (record_.sequence == std::numeric_limits<uint64_t>::max()) return false;
    // A failed publication keeps the same exact intended sequence for retry.
    if (!store_.save(slot_, record_)) return false;
    dirty_ = false;
    ++revision_;
  }
  if (indexDirty_) {
    if (!store_.saveIndex(index_)) return false;
    indexDirty_ = false;
    ++revision_;
  }
  return true;
}
uint64_t SeriesQueue::start(storage::SeriesQueueRecord& reviewed) {
  if (!ready_ || !allowed() || engine_.busy() || dirty_ || indexDirty_ || !connected() ||
      !ports_.reviewCurrent(ports_.context, reviewed) || !reviewed.count || reviewed.count > SeriesNextMax ||
      !std::memchr(reviewed.request.key.data(), 0, reviewed.request.key.size()))
    return 0;
  for (const auto& slot : index_.slots)
    if (slot.id && !slot.terminal && std::strcmp(slot.series.data(), reviewed.request.key.data()) == 0) return slot.id;
  int target = -1;
  uint64_t oldest = std::numeric_limits<uint64_t>::max();
  for (unsigned i = 0; i < 4; ++i) {
    const auto& s = index_.slots[i];
    if (!s.id) {
      target = i;
      break;
    }
    if (s.terminal && s.id < oldest) {
      target = i;
      oldest = s.id;
    }
  }
  if (target < 0 || index_.counter == std::numeric_limits<uint64_t>::max() ||
      index_.sequence == std::numeric_limits<uint64_t>::max())
    return 0;
  for (uint8_t i = 0; i < reviewed.count; ++i) {
    for (uint8_t j = 0; j < i; ++j)
      if (reviewed.books[i].key == reviewed.books[j].key) return 0;
  }
  reviewed.id = index_.counter + 1;
  reviewed.sequence = 1;
  for (uint8_t i = 0; i < reviewed.count; ++i) {
    auto& row = reviewed.books[i];
    row.job = 0;
    row.cancelWanted = false;
    row.error = DownloadError::None;
    if (row.state != SeriesItemState::Unsupported) row.state = SeriesItemState::Queued;
  }
  // Record first, registry second, execution last. An orphan never starts.
  if (!store_.save(target, reviewed)) return 0;
  auto& added = index_.slots[target];
  added.id = reviewed.id;
  added.series = reviewed.request.key;
  added.terminal = false;
  added.cancelMask = 0;
  index_.counter = reviewed.id;
  ++index_.sequence;
  indexDirty_ = true;
  if (!checkpoint()) {
    LOG_ERR("BSQ", "Unacknowledged Series registry retained");
    return 0;
  }
  requested_[target] = true;
  attempted_[target] = 0;
  ++revision_;
  return added.id;
}
bool SeriesQueue::resume(uint64_t id) {
  const int target = locate(id);
  if (!ready_ || target < 0 || !allowed() || !connected() || dirty_ || indexDirty_ || index_.slots[target].terminal)
    return false;
  requested_[target] = true;
  attempted_[target] = 0;
  ++revision_;
  return true;
}
bool SeriesQueue::bind(uint8_t target) {
  if (engine_.busy() || dirty_ || indexDirty_ || !store_.load(target, index_.slots[target].id, record_)) return false;
  for (unsigned i = 0; i < 3; ++i)
    std::snprintf(journal_[i].data(), journal_[i].size(), "%s.%u%s", prefix_.data(), unsigned(target),
                  i == 1   ? ".tmp"
                  : i == 2 ? ".bak"
                           : "");
  if (bound_ != target && !engine_.selectJournal({journal_[0].data(), journal_[1].data(), journal_[2].data()},
                                                 {server_.data(), account_.data()}, owner_))
    return false;
  bound_ = target;
  slot_ = target;
  row_ = SeriesNextMax;
  return true;
}
void SeriesQueue::retire() {
  if (slot_ >= 4) return;
  bool terminal = true;
  for (uint8_t i = 0; i < record_.count; ++i)
    if (!queueTerminal(record_.books[i].state)) terminal = false;
  if (terminal && !index_.slots[slot_].terminal) {
    index_.slots[slot_].terminal = true;
    ++index_.sequence;
    indexDirty_ = true;
  }
  if (terminal) requested_[slot_] = false;
  if (!checkpoint()) return;
  cursor_ = (slot_ + 1) % 4;
  slot_ = 4;
  row_ = SeriesNextMax;
}
bool SeriesQueue::cancel(uint64_t id, uint8_t which) {
  const int target = locate(id);
  if (!ready_ || target < 0 || which > SeriesNextMax || !allowed() || ((dirty_ || indexDirty_) && !engine_.busy()))
    return false;
  auto& entry = index_.slots[target];
  if (entry.terminal) return true;
  entry.cancelMask |= which == SeriesNextMax ? 1023 : uint16_t(1u << which);
  ++index_.sequence;
  indexDirty_ = true;
  if (slot_ == target && row_ < record_.count && (entry.cancelMask & (1u << row_)))
    engine_.cancel(record_.books[row_].job, owner_);
  if (!engine_.busy() && !checkpoint()) return false;
  if (slot_ == target && !engine_.busy()) applyCancel();
  return true;
}
bool SeriesQueue::applyCancel() {
  if (slot_ >= 4) return false;
  const auto mask = index_.slots[slot_].cancelMask;
  if (!mask) return true;
  bool changed = false;
  for (uint8_t i = 0; i < record_.count; ++i) {
    auto& row = record_.books[i];
    if (!(mask & (1u << i)) || queueTerminal(row.state)) continue;
    row.cancelWanted = true;
    changed = true;
    if (engine_.matchesSelection(row.file, owner_)) {
      const auto status = engine_.status(owner_);
      engine_.cancel(status.id, owner_);
      if (engine_.busy()) {
        row_ = i;
        row.job = status.id;
      } else
        row.state = SeriesItemState::Canceled;
    } else
      row.state = SeriesItemState::Canceled;
  }
  if (changed) {
    ++record_.sequence;
    dirty_ = true;
  }
  if (!checkpoint()) return false;
  index_.slots[slot_].cancelMask = 0;
  ++index_.sequence;
  indexDirty_ = true;
  return checkpoint();
}
void SeriesQueue::tick(DeliveryTicket current) {
  if (!ready_) return;
  if (!queueTicket(current, owner_)) {
    // The transfer engine observes the same changed owner and drains privately.
    if (engine_.busy()) engine_.tick(current);
    requested_.fill(false);
    ready_ = false;
    return;
  }
  if (!allowed()) return;
  if (!engine_.busy() && !checkpoint()) return;
  if (engine_.busy()) {
    if (slot_ >= 4 || (row_ >= record_.count && !preparing_)) return;  // Another feature owns this writer.
    engine_.tick(owner_);
    ++revision_;
    if (engine_.busy()) return;
  }
  preparing_ = false;
  if (slot_ < 4 && row_ < record_.count) {
    auto& row = record_.books[row_];
    const auto status = engine_.status(owner_);
    row.error = status.error;
    if (status.state == DownloadState::Complete)
      row.state = SeriesItemState::Complete;
    else if (status.state == DownloadState::Canceled)
      row.state = SeriesItemState::Canceled;
    else if (status.state == DownloadState::Paused)
      row.state = SeriesItemState::Paused;
    else
      row.state = SeriesItemState::Failed;
    attempted_[slot_] |= uint16_t(1u << row_);
    if (row.state == SeriesItemState::Paused || (!connected() && row.state == SeriesItemState::Failed))
      requested_[slot_] = false;
    ++record_.sequence;
    dirty_ = true;
    if (!checkpoint()) return;
    row_ = SeriesNextMax;
    retire();
    return;
  }
  if (slot_ >= 4) {
    for (unsigned n = 0; n < 4; ++n) {
      const uint8_t next = (cursor_ + n) % 4;
      if ((requested_[next] || index_.slots[next].cancelMask) && !index_.slots[next].terminal) {
        if (!bind(next)) {
          requested_[next] = false;
          LOG_ERR("BSQ", "Series queue bind failed");
          return;
        }
        break;
      }
    }
  }
  if (slot_ >= 4) return;
  if (!applyCancel()) return;
  if (engine_.busy()) return;
  if (!connected()) {
    requested_[slot_] = false;
    retire();
    return;
  }
  if (!requested_[slot_]) {
    retire();
    return;
  }
  for (uint8_t i = 0; i < record_.count; ++i) {
    auto& row = record_.books[i];
    if (queueTerminal(row.state)) continue;
    // Retry each failed file at most once per explicit Resume, while continuing
    // other files after ordinary errors. Paused/offline stops the whole queue.
    if (row.state == SeriesItemState::Failed && (attempted_[slot_] & (1u << i))) continue;
    if (row.cancelWanted) {
      row.state = SeriesItemState::Canceled;
      ++record_.sequence;
      dirty_ = true;
      checkpoint();
      return;
    }
    const auto previous = engine_.status(owner_);
    const bool match = engine_.matchesSelection(row.file, owner_) && previous.state != DownloadState::Canceled;
    if (!match && previous.id && previous.state != DownloadState::Complete &&
        previous.state != DownloadState::Canceled) {
      engine_.cancel(previous.id, owner_);
      preparing_ = engine_.busy();
      if (engine_.status(owner_).state != DownloadState::Canceled && !preparing_) {
        requested_[slot_] = false;
        retire();
      }
      return;
    }
    row.job = match ? previous.id : engine_.start(row.file, source_, owner_);
    const bool started = match ? engine_.resume(row.job, source_, owner_) : row.job != 0;
    row.state = started ? SeriesItemState::Running : SeriesItemState::Failed;
    row.error = started ? DownloadError::None : engine_.status(owner_).error;
    row_ = started ? i : SeriesNextMax;
    ++record_.sequence;
    dirty_ = true;
    if (!engine_.busy()) checkpoint();
    if (!started) {
      requested_[slot_] = false;
      retire();
    }
    return;
  }
  requested_[slot_] = false;
  retire();
}
void SeriesQueue::queueState(SeriesQueueView& out) {
  out.complete = true;
  out.paused = false;
  out.sequence = revision_;
  const int target = locate(out.id);
  for (uint8_t i = 0; i < out.count; ++i) {
    auto& row = out.items[i];
    if (target >= 0 && (index_.slots[target].cancelMask & (1u << i)) && !queueTerminal(row.state)) {
      row.cancelWanted = true;
      if (row.state != SeriesItemState::Running) row.state = SeriesItemState::Canceled;
    }
    if (!queueTerminal(row.state)) {
      out.complete = false;
      if (target < 0 || !requested_[target]) {
        out.paused = true;
        if (row.state == SeriesItemState::Queued || row.state == SeriesItemState::Running)
          row.state = SeriesItemState::Paused;
      }
    }
  }
  if (slot_ < 4 && index_.slots[slot_].id == out.id && row_ < out.count && engine_.busy()) {
    const auto status = engine_.status(owner_);
    out.active = row_;
    out.received = status.received;
    out.total = status.total;
    out.totalKnown = status.totalKnown;
  }
}
void SeriesQueue::projectRecord(SeriesQueueView& out) {
  out.clear();
  out.id = record_.id;
  out.sequence = record_.sequence;
  out.series = record_.request.key;
  out.count = record_.count;
  for (uint8_t i = 0; i < out.count; ++i) {
    const auto& source = record_.books[i];
    auto& row = out.items[i];
    row.key = source.key;
    row.title = source.title;
    row.destination = source.destination;
    row.variant = source.variantLabel;
    row.job = source.job;
    row.bytes = source.file.bytes;
    row.sizeKnown = source.file.sizeKnown;
    row.state = source.state;
    row.error = source.error;
    row.cancelWanted = source.cancelWanted;
  }
  out.availability = Availability::Ready;
}
bool SeriesQueue::read(uint64_t id, SeriesQueueView& out) {
  const int target = locate(id);
  if (!ready_ || target < 0 || !allowed()) return false;
  // Foreground Library keeps its last published value while an unrelated file
  // owns the only SD handle. A deferred read never erases that value.
  if (slot_ == target)
    projectRecord(out);
  else {
    if (engine_.busy()) return false;
    if (!store_.project(target, id, out)) return false;
  }
  queueState(out);
  return true;
}
}  // namespace booksync

// Link reachability root for the dormant coordinator seam, never called by boot.
extern "C" bool booksync_series_queue_link(booksync::SeriesQueue& worker, booksync::storage::SeriesQueueRecord& review,
                                           booksync::SeriesQueueView& out, booksync::DeliveryTicket owner) {
  if (!worker.recover()) return false;
  const auto id = worker.start(review);
  worker.tick(owner);
  if (!worker.read(id, out)) return false;
  if (out.paused) worker.resume(id);
  if (out.complete) worker.cancel(id);
  return id != 0;
}
