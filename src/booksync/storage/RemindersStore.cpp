// SPDX-License-Identifier: GPL-3.0-or-later
#include "RemindersStore.h"

#include <Logging.h>

#include <cstring>
namespace booksync::storage {
namespace {
constexpr uint32_t Magic = 0x524d5342, Version = 1, Payload = 2482, Bytes = Payload + 16;
static_assert(Bytes <= ReminderStoreBytes);
struct Codec {
  FsFile* file = nullptr;
  FilePublish* output = nullptr;
  bool reading = false, ok = true, comparing = false;
  uint32_t hash = 2166136261u;
  void writeBytes(const void* data, size_t n) {
    if (!ok) return;
    if (comparing) {
      const auto* expected = static_cast<const uint8_t*>(data);
      uint8_t actual[8]{};
      for (size_t offset = 0; offset < n;) {
        const size_t count = n - offset < sizeof(actual) ? n - offset : sizeof(actual);
        if (file->read(actual, count) != static_cast<int>(count) || std::memcmp(actual, expected + offset, count)) {
          ok = false;
          return;
        }
        offset += count;
      }
    } else if (output)
      ok = output->write(data, n);
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; ++i) hash = (hash ^ p[i]) * 16777619u;
  }
  void bytes(void* data, size_t n) {
    if (!ok) return;
    if (comparing) {
      writeBytes(data, n);
      return;
    }
    if (reading)
      ok = file->read(data, n) == static_cast<int>(n);
    else if (output)
      ok = output->write(data, n);
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; ++i) hash = (hash ^ p[i]) * 16777619u;
  }
  uint64_t number(uint64_t value, unsigned n) {
    uint8_t b[8]{};
    if (!reading)
      for (unsigned i = 0; i < n; ++i) b[i] = value >> (i * 8);
    bytes(b, n);
    value = 0;
    for (unsigned i = 0; i < n; ++i) value |= uint64_t(b[i]) << (i * 8);
    return value;
  }
  void writeRecord(const ReminderRecord& r) {
    number(r.id, 8);
    number(r.revision, 8);
    number(r.mutation, 8);
    number(r.after, 8);
    number(r.snooze, 8);
    number(r.snoozeScheduled, 8);
    number(static_cast<uint32_t>(r.highWater), 4);
    number(static_cast<uint32_t>(r.snoozeDay), 4);
    number(r.sequence, 4);
    number(r.fields.enabled, 1);
    number(r.fields.minute, 2);
    number(r.fields.days, 1);
    writeBytes(r.fields.message, sizeof(r.fields.message));
  }
  void record(ReminderRecord& r) {
    r.id = number(r.id, 8);
    r.revision = number(r.revision, 8);
    r.mutation = number(r.mutation, 8);
    r.after = static_cast<int64_t>(number(r.after, 8));
    r.snooze = static_cast<int64_t>(number(r.snooze, 8));
    r.snoozeScheduled = static_cast<int64_t>(number(r.snoozeScheduled, 8));
    r.highWater = static_cast<int32_t>(number(static_cast<uint32_t>(r.highWater), 4));
    r.snoozeDay = static_cast<int32_t>(number(static_cast<uint32_t>(r.snoozeDay), 4));
    r.sequence = number(r.sequence, 4);
    const auto enabled = number(r.fields.enabled, 1);
    if (enabled > 1) ok = false;
    r.fields.enabled = enabled != 0;
    r.fields.minute = number(r.fields.minute, 2);
    r.fields.days = number(r.fields.days, 1);
    bytes(r.fields.message, sizeof(r.fields.message));
  }
};
void payload(Codec& c, const ReminderData& data) {
  c.number(data.nextId, 8);
  c.number(data.intervalRevision, 8);
  c.number(data.intervalMutation, 8);
  c.number(data.deletedId, 8);
  c.number(data.deletedKey, 8);
  c.number(data.interval, 1);
  c.number(data.enabled, 1);
  for (const auto& row : data.rows) {
    c.writeRecord(row);
  }
}
}  // namespace
bool RemindersStore::validate(FsFile& file, void* context) {
  return static_cast<RemindersStore*>(context)->read(file, nullptr);
}
bool RemindersStore::read(FsFile& file, ReminderData* data) {
#if SIZE_MAX < UINT64_MAX
  if (file.fileSize64() != Bytes) return false;
#else
  if (file.fileSize() != Bytes) return false;
#endif
  Codec c{&file, nullptr, true};
  if (c.number(0, 4) != Magic || c.number(0, 4) != Version || c.number(0, 4) != Payload) return false;
  const auto checksum = c.number(0, 4);
  c.hash = 2166136261u;
  const auto nextId = c.number(0, 8), revision = c.number(0, 8), mutation = c.number(0, 8);
  const auto deleted = c.number(0, 8), deletedKey = c.number(0, 8);
  const auto interval = c.number(0, 1), enabled = c.number(0, 1);
  if (((deleted == 0) != (deletedKey == 0)) || (deleted && (deleted < 2 || deleted >= nextId)) || nextId < 2 ||
      !revision || enabled > 1 || !(interval == 20 || interval == 30 || interval == 45 || interval == 60))
    return false;
  if (data) {
    data->nextId = nextId;
    data->intervalRevision = revision;
    data->intervalMutation = mutation;
    data->deletedId = deleted;
    data->deletedKey = deletedKey;
    data->interval = interval;
    data->enabled = enabled;
  }
  uint64_t ids[8]{};
  for (unsigned i = 0; i < 8; ++i) {
    scratch_ = {};
    c.record(scratch_);
    const auto& r = scratch_;
    if (!c.ok) return false;
    if (!Reminders::validRecord(r, nextId) || (r.id && r.id == deleted)) return false;
    for (unsigned j = 0; j < i; ++j)
      if (r.id && ids[j] == r.id) return false;
    ids[i] = r.id;
    if (data) data->rows[i] = r;
  }
  return c.ok && c.hash == checksum;
}
ReminderStorage RemindersStore::load(ReminderData& data) {
  if (!Storage.ready()) {
    LOG_ERR("BSR", "Reminder storage unavailable");
    return ReminderStorage::Unreadable;
  }
  const auto selected = FilePublish::select(paths_, validate, this);
  if (selected == PublishedFile::Missing) {
    for (auto& row : data.rows) row = {};
    data.nextId = 2;
    data.intervalRevision = 1;
    data.intervalMutation = 0;
    data.deletedId = data.deletedKey = 0;
    data.interval = 20;
    data.enabled = false;
    return ReminderStorage::Ready;
  }
  if (selected == PublishedFile::Unreadable) {
    LOG_ERR("BSR", "Reminders unreadable; retaining source bytes");
    return ReminderStorage::Unreadable;
  }
  auto file = Storage.open(selected == PublishedFile::Current ? paths_.current : paths_.backup, O_RDONLY);
  const bool valid = file && read(file, &data);
  const bool closed = file.close();
  if (!valid || !closed) {
    LOG_ERR("BSR", "Cannot read selected reminders");
    return ReminderStorage::Unreadable;
  }
  return selected == PublishedFile::Backup ? ReminderStorage::Recovered : ReminderStorage::Ready;
}
bool RemindersStore::save(const ReminderData& data) {
  if (!Storage.ready()) {
    LOG_ERR("BSR", "Reminder storage unavailable");
    return false;
  }
  if (!Reminders::valid(data)) {
    LOG_ERR("BSR", "Invalid reminder state");
    return false;
  }
  // Preserve damaged committed bytes even after backup recovery. Repair requires
  // a separate explicit recovery operation; tmp is never a committed schedule.
  if (Storage.exists(paths_.current) && !FilePublish::validateCurrent(paths_, validate, this)) {
    LOG_ERR("BSR", "Damaged reminder primary retained");
    return false;
  }
  Codec hash;
  payload(hash, data);
  FilePublish writer(paths_, validate, this);
  if (!writer.begin(Bytes)) return false;
  Codec out{nullptr, &writer};
  out.number(Magic, 4);
  out.number(Version, 4);
  out.number(Payload, 4);
  out.number(hash.hash, 4);
  payload(out, data);
  if (!out.ok) return false;
  if (writer.finish()) return true;
  // Reconcile exact intended bytes, not just the non-cryptographic checksum:
  // a valid older envelope can have the same hash. One reader and 8-byte compare
  // scratch preserve both the single-open-file rule and the worker stack budget.
  auto file = Storage.open(paths_.current, O_RDONLY);
  bool matches = false;
  if (file) {
    Codec check{&file};
    check.comparing = true;
#if SIZE_MAX < UINT64_MAX
    check.ok = file.fileSize64() == Bytes;
#else
    check.ok = file.fileSize() == Bytes;
#endif
    check.number(Magic, 4);
    check.number(Version, 4);
    check.number(Payload, 4);
    check.number(hash.hash, 4);
    payload(check, data);
    matches = check.ok;
  }
  const bool closed = file.close();
  return matches && closed && writer.confirm();
}
}  // namespace booksync::storage
