// SPDX-License-Identifier: GPL-3.0-or-later
#include "Reminders.h"

#include <Logging.h>

#include <cstring>
#include <limits>
namespace booksync {
namespace {
constexpr uint64_t Break = 300000;
constexpr int64_t Day = 86400;
bool validText(const char* text) {
  unsigned chars = 0, bytes = 0;
  while (bytes < 241 && text[bytes]) {
    const auto c = static_cast<unsigned char>(text[bytes++]);
    if (c < 32 || c == 127) return false;
    uint32_t value = c;
    unsigned continuation = 0;
    if (c >= 0xc2 && c <= 0xdf) {
      value = c & 31;
      continuation = 1;
    } else if (c >= 0xe0 && c <= 0xef) {
      value = c & 15;
      continuation = 2;
    } else if (c >= 0xf0 && c <= 0xf4) {
      value = c & 7;
      continuation = 3;
    } else if (c >= 128)
      return false;
    const unsigned length = continuation;
    while (continuation--) {
      if (bytes >= 240) return false;
      const auto next = static_cast<unsigned char>(text[bytes++]);
      if ((next & 0xc0) != 0x80) return false;
      value = (value << 6) | (next & 63);
    }
    if ((length == 1 && value < 128) || (length == 2 && value < 2048) || (length == 3 && value < 65536) ||
        value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff) || (value >= 128 && value <= 159) || value == 0x2028 ||
        value == 0x2029)
      return false;
    if (++chars > 60) return false;
  }
  return bytes <= 240 && text[bytes] == 0;
}
bool same(const ReminderDue& a, const ReminderDue& b) {
  return a.source == b.source && a.id == b.id && a.revision == b.revision && a.kind == b.kind && a.day == b.day &&
         a.sequence == b.sequence && a.session == b.session && a.effective == b.effective;
}
}  // namespace
bool Reminders::valid(const ReminderFields& f) {
  if (f.kind == ReminderKind::Interval)
    return !f.minute && !f.days && !f.message[0] &&
           (f.intervalMinutes == 20 || f.intervalMinutes == 30 || f.intervalMinutes == 45 || f.intervalMinutes == 60);
  return f.kind == ReminderKind::Time && !f.intervalMinutes && f.minute < 1440 && f.days && f.days < 128 &&
         validText(f.message);
}
bool Reminders::validRecord(const ReminderRecord& r, uint64_t nextId) {
  if (!r.id) {
    if (r.revision || r.mutation || r.after || r.snooze || r.snoozeScheduled || r.snoozeDay != -1 ||
        r.highWater != -1 || r.sequence || r.fields.kind != ReminderKind::Time || r.fields.enabled || r.fields.minute ||
        r.fields.days || r.fields.intervalMinutes)
      return false;
    for (char byte : r.fields.message)
      if (byte) return false;
    return true;
  }
  const bool snoozeValid =
      !r.snooze ? r.snoozeDay == -1 && r.snoozeScheduled == 0
                : r.fields.enabled && r.sequence && r.snoozeDay >= 0 && r.snoozeDay <= 47482 &&
                      r.highWater >= r.snoozeDay && r.snoozeScheduled >= 0 && r.snoozeScheduled <= 4102444800LL &&
                      r.snoozeScheduled - (int64_t(r.snoozeDay) * Day + int64_t(r.fields.minute) * 60) >= -840 * 60 &&
                      r.snoozeScheduled - (int64_t(r.snoozeDay) * Day + int64_t(r.fields.minute) * 60) <= 840 * 60;
  return snoozeValid && r.id >= 2 && r.id < nextId && r.revision && r.mutation && r.fields.kind == ReminderKind::Time &&
         valid(r.fields) && r.highWater >= -1 && r.highWater <= 47482 && r.after >= -1 && r.after <= 4102444800LL &&
         r.snooze >= 0 && r.snooze <= 4102445400LL;
}
bool Reminders::valid(const ReminderData& d) {
  if (d.nextId < 2 || !d.intervalRevision ||
      !(d.interval == 20 || d.interval == 30 || d.interval == 45 || d.interval == 60) ||
      ((d.deletedId == 0) != (d.deletedKey == 0)) || (d.deletedId && (d.deletedId < 2 || d.deletedId >= d.nextId)))
    return false;
  for (unsigned i = 0; i < 8; ++i) {
    const auto& r = d.rows[i];
    if (!validRecord(r, d.nextId) || (r.id && r.id == d.deletedId)) return false;
    for (unsigned j = 0; j < i; ++j)
      if (r.id && d.rows[j].id == r.id) return false;
  }
  return true;
}
ReminderStorage Reminders::load() {
  live_ = waiting_ = false;
  for (auto& target : snoozeElapsed_) target = 0;
  for (auto& start : activationElapsed_) start = 0;
  resetInterval();
  storage_ = port_.load(port_.context, data_);
  return storage_;
}
bool Reminders::save() {
  if (storage_ == ReminderStorage::Unreadable || !port_.save(port_.context, data_)) {
    LOG_ERR("BSR", "Reminder state not durably saved");
    return false;
  }
  return true;
}
unsigned Reminders::count() const {
  unsigned n = 0;
  for (const auto& r : data_.rows) n += r.id != 0;
  return n;
}
uint64_t Reminders::nextMutation() const {
  uint64_t high = data_.intervalMutation > data_.deletedKey ? data_.intervalMutation : data_.deletedKey;
  for (const auto& row : data_.rows)
    if (row.mutation > high) high = row.mutation;
  return high + 1;
}
void Reminders::resetInterval() {
  interval_.active = 0;
  interval_.ready = false;
  ++interval_.generation;
  ++interval_.sequence;
}
ReminderResult Reminders::mutate(const ReminderMutation& m, const ReminderNow& now) {
  if (storage_ == ReminderStorage::Unreadable) return {ReminderStatus::StorageError};
  if (!m.key || m.key == UINT64_MAX ||
      static_cast<unsigned>(m.operation) > static_cast<unsigned>(ReminderOperation::Delete))
    return {ReminderStatus::Invalid};
  if (m.id == 1 || m.fields.kind == ReminderKind::Interval) {
    if (m.id != 1 || m.operation == ReminderOperation::Add || m.operation == ReminderOperation::Delete)
      return {ReminderStatus::Invalid};
    if (m.key == data_.intervalMutation) return {ReminderStatus::Saved, 1, data_.intervalRevision};
    if (m.key < nextMutation()) return {ReminderStatus::Stale};
    if (m.revision != data_.intervalRevision) return {ReminderStatus::Stale};
    const bool fieldsValid = m.operation == ReminderOperation::SetEnabled
                                 ? m.fields.kind == ReminderKind::Interval && !m.fields.minute && !m.fields.days &&
                                       !m.fields.message[0] && !m.fields.intervalMinutes
                                 : m.fields.kind == ReminderKind::Interval && valid(m.fields);
    if (!fieldsValid) return {ReminderStatus::Invalid};
    const auto rev = data_.intervalRevision, key = data_.intervalMutation;
    const auto minutes = data_.interval;
    const bool enabled = data_.enabled;
    if (m.operation == ReminderOperation::Edit) data_.interval = m.fields.intervalMinutes;
    data_.enabled = m.fields.enabled;
    ++data_.intervalRevision;
    data_.intervalMutation = m.key;
    if (!save()) {
      data_.intervalRevision = rev;
      data_.intervalMutation = key;
      data_.interval = minutes;
      data_.enabled = enabled;
      return {ReminderStatus::StorageError};
    }
    resetInterval();
    if (live_ && grant_.kind == ReminderKind::Interval) live_ = false;
    return {ReminderStatus::Saved, 1, data_.intervalRevision};
  }
  if (m.operation == ReminderOperation::Delete && m.id == data_.deletedId && m.key == data_.deletedKey)
    return {ReminderStatus::Saved, m.id};
  ReminderRecord* row = nullptr;
  for (auto& r : data_.rows) {
    if (r.id && r.mutation == m.key && (r.id == m.id || m.operation == ReminderOperation::Add))
      return {ReminderStatus::Saved, r.id, r.revision};
    if (m.operation == ReminderOperation::Add ? !r.id : r.id == m.id) row = &r;
  }
  if (m.key < nextMutation()) return {ReminderStatus::Stale};
  if (m.operation == ReminderOperation::Add && m.id) return {ReminderStatus::Invalid};
  if (!row) return {m.operation == ReminderOperation::Add ? ReminderStatus::Limit : ReminderStatus::Stale};
  if (m.operation != ReminderOperation::Add && m.revision != row->revision) return {ReminderStatus::Stale};
  if (m.operation == ReminderOperation::SetEnabled) {
    if (m.fields.kind != ReminderKind::Time || m.fields.minute || m.fields.days || m.fields.message[0] ||
        m.fields.intervalMinutes)
      return {ReminderStatus::Invalid};
  } else if (m.operation != ReminderOperation::Delete && !valid(m.fields))
    return {ReminderStatus::Invalid};
  // One <=320-byte rollback record is the bounded worker transaction scratch;
  // copying the complete schedule would exceed both stack and scratch budgets.
  const ReminderRecord before = *row;
  const auto next = data_.nextId, deletedId = data_.deletedId, deletedKey = data_.deletedKey;
  if (m.operation == ReminderOperation::Delete) {
    data_.deletedId = row->id;
    data_.deletedKey = m.key;
    *row = {};
  } else {
    if (m.operation == ReminderOperation::Add) row->id = data_.nextId++;
    if (m.operation == ReminderOperation::SetEnabled)
      row->fields.enabled = m.fields.enabled;
    else
      row->fields = m.fields;
    ++row->revision;
    row->mutation = m.key;
    row->after = now.known && now.wall > 0 ? now.wall : -1;
    row->snooze = 0;
    row->snoozeDay = -1;
    row->snoozeScheduled = 0;
  }
  if (!save()) {
    *row = before;
    data_.nextId = next;
    data_.deletedId = deletedId;
    data_.deletedKey = deletedKey;
    return {ReminderStatus::StorageError};
  }
  snoozeElapsed_[row - data_.rows] = 0;
  activationElapsed_[row - data_.rows] = now.known ? 0 : now.elapsed + 1;
  if (live_ && grant_.id == before.id) live_ = false;
  return {ReminderStatus::Saved, m.operation == ReminderOperation::Delete ? before.id : row->id, row->revision};
}
void Reminders::activity(const ReminderNow& now, bool input) {
  if (input) waiting_ = false;
  auto& s = interval_;
  const bool page = now.page && now.visible && !now.sleeping && !now.modal && !live_;
  if (!s.initialized || now.elapsed < s.last) {
    resetInterval();
    s.last = s.idle = now.elapsed;
    s.page = page;
    s.initialized = true;
  }
  const uint64_t gap = now.elapsed - s.idle;
  if (gap >= Break) {
    if (!s.idleReset) resetInterval();
    s.idleReset = true;
    // Preserve the idle start until genuine input returns. A timer is not input.
  } else if (s.page && page && data_.enabled) {
    s.active += now.elapsed - s.last;
  }
  if (s.page && !page && gap < Break) s.active += now.elapsed - s.last;
  if (s.page && !page) s.idle = now.elapsed;
  if (input && page) {
    s.idle = now.elapsed;
    s.idleReset = false;
  }
  if (page && !s.page && gap < Break) s.idle = now.elapsed;
  if (s.ready && !page) resetInterval();
  s.page = page;
  s.last = now.elapsed;
  if (page && data_.enabled && s.active >= uint64_t(data_.interval) * 60000) s.ready = true;
}
bool Reminders::next(const ReminderNow& now, ReminderDue& out) {
  activity(now);
  if (storage_ == ReminderStorage::Unreadable || live_ || !now.visible || now.sleeping || now.modal || waiting_)
    return false;
  bool found = false;
  if (now.known && now.wall >= 0 && now.offsetMinutes >= -840 && now.offsetMinutes <= 840) {
    const int64_t local = now.wall + int64_t(now.offsetMinutes) * 60;
    const int32_t today = static_cast<int32_t>(local / Day);
    for (unsigned i = 0; i < 8; ++i) {
      auto& r = data_.rows[i];
      if (!r.id || !r.fields.enabled) continue;
      if (r.after == -1) {
        // A mutation made without wall time uses this boot's elapsed evidence.
        // After cold loss that evidence is absent: never invent an earlier save
        // instant. Start after recovery rather than backfiring a past schedule.
        const auto start = activationElapsed_[i];
        r.after = start && now.elapsed >= start - 1
                      ? now.wall - static_cast<int64_t>((now.elapsed - (start - 1)) / 1000)
                      : now.wall;
        if (r.after < 0) r.after = 0;
      }
      for (unsigned option = 0; option < 3; ++option) {
        const bool snooze = option == 2;
        const int32_t date = snooze ? r.snoozeDay : today - static_cast<int32_t>(option);
        const int64_t scheduled =
            snooze ? r.snoozeScheduled
                   : int64_t(date) * Day + int64_t(r.fields.minute) * 60 - int64_t(now.offsetMinutes) * 60;
        int64_t due = snooze ? r.snooze : scheduled;
        if (snooze) {
          if (!r.snooze) continue;
          if (snoozeElapsed_[i]) {
            const auto target = snoozeElapsed_[i];
            if (now.elapsed < target || now.elapsed - target > 3600000) continue;
            due = now.wall - static_cast<int64_t>((now.elapsed - target) / 1000);
          }
        } else if (date <= r.highWater || scheduled <= r.after || !(r.fields.days & (1u << ((date + 3) % 7))))
          continue;
        if (due > now.wall || now.wall - due > 3600) continue;
        if (found && (due > out.effective || (due == out.effective && r.id >= out.id))) continue;
        out = {};
        out.id = r.id;
        out.revision = r.revision;
        out.day = date;
        out.scheduled = scheduled;
        out.effective = due;
        out.sequence = snooze ? r.sequence : 0;
        out.missed = now.wall / 60 > due / 60;
        std::memcpy(out.message, r.fields.message, sizeof(out.message));
        found = true;
      }
    }
  }
  if (found) {
    if (interval_.ready) resetInterval();
    return true;
  }
  if (!interval_.ready || !now.page || !data_.enabled) return false;
  out = {};
  out.kind = ReminderKind::Interval;
  out.id = 1;
  out.revision = data_.intervalRevision;
  out.session = interval_.generation;
  out.sequence = interval_.sequence;
  return true;
}
ReminderResult Reminders::claim(const ReminderDue& candidate, uint64_t owner, const ReminderNow& now) {
  // One bounded arbitration value (<=1 KiB), reused for the claim check;
  // keeping it on the serialized worker stack avoids a resident event copy.
  ReminderDue current{};
  if (serial_ == UINT64_MAX) {
    LOG_ERR("BSR", "Reminder grant identity exhausted");
    return {ReminderStatus::Unavailable};
  }
  if (!owner || !next(now, current) || !same(candidate, current)) return {ReminderStatus::Stale};
  if (current.kind == ReminderKind::Time) {
    for (auto& r : data_.rows)
      if (r.id == current.id) {
        const auto high = r.highWater;
        const auto snooze = r.snooze;
        const auto snoozeDay = r.snoozeDay;
        const auto snoozeScheduled = r.snoozeScheduled;
        if (!current.sequence && current.day > r.highWater) r.highWater = current.day;
        if (current.sequence) {
          r.snooze = 0;
          r.snoozeDay = -1;
          r.snoozeScheduled = 0;
        }
        if (!save()) {
          r.highWater = high;
          r.snooze = snooze;
          r.snoozeDay = snoozeDay;
          r.snoozeScheduled = snoozeScheduled;
          return {ReminderStatus::StorageError};
        }
      }
  } else
    resetInterval();
  grant_ = {current.kind, current.id, current.revision, current.day, current.scheduled};
  owner_ = owner;
  token_ = ++serial_;
  live_ = true;
  interval_.page = false;
  interval_.last = interval_.idle = now.elapsed;
  return {ReminderStatus::Saved, current.id, current.revision, token_};
}
ReminderResult Reminders::respond(uint64_t token, uint64_t owner, ReminderAction action, const ReminderNow& now) {
  if (!live_ || token != token_ || owner != owner_) return {ReminderStatus::Stale};
  if (static_cast<unsigned>(action) > static_cast<unsigned>(ReminderAction::TurnOff)) return {ReminderStatus::Invalid};
  if (grant_.kind == ReminderKind::Interval) {
    if (action != ReminderAction::Done && action != ReminderAction::TurnOff) return {ReminderStatus::Invalid};
    if (action == ReminderAction::TurnOff) {
      ReminderMutation m{};
      m.id = 1;
      m.revision = data_.intervalRevision;
      m.key = nextMutation();
      m.operation = ReminderOperation::SetEnabled;
      m.fields.kind = ReminderKind::Interval;
      const auto result = mutate(m, now);
      if (result.status != ReminderStatus::Saved) return result;
    }
    resetInterval();
  } else {
    if (action == ReminderAction::Done || action == ReminderAction::TurnOff) return {ReminderStatus::Invalid};
    for (unsigned i = 0; i < 8; ++i)
      if (data_.rows[i].id == grant_.id) {
        auto& r = data_.rows[i];
        if (r.revision != grant_.revision) return {ReminderStatus::Stale};
        if (action == ReminderAction::Snooze10) {
          if (!now.known) return {ReminderStatus::Unavailable};
          if (r.sequence == UINT32_MAX) return {ReminderStatus::Unavailable};
          const auto before = r.snooze;
          const auto beforeDay = r.snoozeDay;
          const auto beforeScheduled = r.snoozeScheduled;
          const auto seq = r.sequence;
          r.snooze = now.wall + 600;
          r.snoozeDay = grant_.day;
          r.snoozeScheduled = grant_.scheduled;
          ++r.sequence;
          if (!save()) {
            r.snooze = before;
            r.snoozeDay = beforeDay;
            r.snoozeScheduled = beforeScheduled;
            r.sequence = seq;
            return {ReminderStatus::StorageError};
          }
          snoozeElapsed_[i] = now.elapsed + 600000;
        }
      }
  }
  live_ = false;
  interval_.page = false;
  interval_.last = now.elapsed;
  waiting_ = true;
  return {ReminderStatus::Saved, grant_.id, grant_.revision, token_, action == ReminderAction::Sleep};
}
void Reminders::retire(uint64_t owner) {
  if (owner_ == owner) live_ = false;
}
}  // namespace booksync
