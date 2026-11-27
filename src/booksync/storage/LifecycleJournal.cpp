// SPDX-License-Identifier: GPL-3.0-or-later
#include "LifecycleJournal.h"

#include <Logging.h>

#include <algorithm>
#include <cstring>
#include <limits>

namespace booksync::storage {
namespace {
constexpr uint32_t Header = 24, Record = 24, Trailer = 4;
constexpr uint32_t MaxBytes = 256 * 1024;
constexpr uint16_t MaxEntries = 64;
constexpr uint32_t Version = 1;
uint64_t get(const uint8_t* p, size_t n) {
  uint64_t value = 0;
  for (size_t i = 0; i < n; ++i) value |= uint64_t(p[i]) << (8 * i);
  return value;
}
void put(uint8_t* p, uint64_t value, size_t n) {
  for (size_t i = 0; i < n; ++i) p[i] = uint8_t(value >> (8 * i));
}
uint32_t crc(uint32_t value, const void* data, size_t n) {
  const auto* p = static_cast<const uint8_t*>(data);
  while (n--) {
    value ^= *p++;
    for (int bit = 0; bit < 8; ++bit) value = (value >> 1) ^ (0xedb88320U & (0U - (value & 1)));
  }
  return value;
}
struct RecordInfo {
  uint64_t id;
  uint16_t lengths[5];
  uint8_t ack;
  uint32_t prefix() const { return lengths[0] + lengths[1] + lengths[2] + lengths[3]; }
  uint32_t size() const { return Record + prefix() + LifecycleJournal::MaxCheckpoint; }
};
bool record(FsFile& file, RecordInfo& out) {
  uint8_t h[Record];
  if (file.read(h, sizeof(h)) != sizeof(h)) return false;
  out.id = get(h, 8);
  for (int i = 0; i < 5; ++i) out.lengths[i] = get(h + 8 + 2 * i, 2);
  out.ack = h[18];
  if (!out.id || out.ack > LifecycleJournal::AllPhases) return false;
  for (int i = 0; i < 3; ++i)
    if (!out.lengths[i] || out.lengths[i] > LifecycleJournal::MaxIdentity) return false;
  for (int i = 19; i < 24; ++i)
    if (h[i]) return false;
  return out.lengths[3] <= LifecycleJournal::MaxPayload && out.lengths[4] <= LifecycleJournal::MaxCheckpoint;
}
RecordInfo info(const JournalEntry& entry, uint64_t id) {
  return {id,
          {uint16_t(entry.adapter.size()), uint16_t(entry.owner.size()), uint16_t(entry.document.size()),
           uint16_t(entry.payload.size()), uint16_t(entry.checkpoint.size())},
          entry.acknowledged};
}
void encode(const RecordInfo& r, uint8_t* h) {
  memset(h, 0, Record);
  put(h, r.id, 8);
  for (int i = 0; i < 5; ++i) put(h + 8 + 2 * i, r.lengths[i], 2);
  h[18] = r.ack;
}
bool close(FsFile& file) { return !file || file.close(); }
JournalResult error(JournalResult result) {
  LOG_ERR("BSJ", "Journal operation blocked: %u", unsigned(result));
  return result;
}
// Streams at most 128 bytes at a time. No journal-sized allocation, on C3 or S3.
class Output {
 public:
  explicit Output(FilePublish& writer) : writer_(writer) {}
  bool write(const void* p, size_t n) {
    checksum_ = crc(checksum_, p, n);
    return writer_.write(p, n);
  }
  bool copy(FsFile& file, size_t n) {
    uint8_t buffer[128];
    while (n) {
      const size_t chunk = std::min(n, sizeof(buffer));
      if (file.read(buffer, chunk) != static_cast<int>(chunk) || !write(buffer, chunk)) return false;
      n -= chunk;
    }
    return true;
  }
  bool zeros(size_t n) {
    const uint8_t buffer[128]{};
    while (n) {
      const auto chunk = std::min(n, sizeof(buffer));
      if (!write(buffer, chunk)) return false;
      n -= chunk;
    }
    return true;
  }
  bool finish() {
    uint8_t bytes[4];
    put(bytes, ~checksum_, 4);
    return writer_.write(bytes, 4);
  }

 private:
  FilePublish& writer_;
  uint32_t checksum_ = ~0U;
};
bool validEntry(const JournalEntry& e) {
  return !e.adapter.empty() && e.adapter.size() <= LifecycleJournal::MaxIdentity && !e.owner.empty() &&
         e.owner.size() <= LifecycleJournal::MaxIdentity && !e.document.empty() &&
         e.document.size() <= LifecycleJournal::MaxIdentity && e.payload.size() <= LifecycleJournal::MaxPayload &&
         e.checkpoint.size() <= LifecycleJournal::MaxCheckpoint && e.acknowledged <= LifecycleJournal::AllPhases;
}
}  // namespace
LifecycleJournal::LifecycleJournal(PublishPaths paths, JournalLimits limits) : paths_(paths), limits_(limits) {}
bool LifecycleJournal::validate(FsFile& file, void*) {
  const auto size = file.fileSize();
  uint8_t h[Header];
  if (size < Header + Trailer || size > MaxBytes || file.read(h, sizeof(h)) != sizeof(h) || memcmp(h, "BSLJ", 4) ||
      get(h + 4, 4) != Version || get(h + 20, 4) != size || !get(h + 8, 8) || get(h + 16, 4) > MaxEntries)
    return false;
  const auto next = get(h + 8, 8), count = get(h + 16, 4);
  uint32_t offset = Header;
  uint64_t prior = 0;
  for (uint32_t i = 0; i < count; ++i) {
    RecordInfo r;
    if (offset > size - Trailer || !file.seekSet(offset) || !record(file, r) || r.id <= prior || r.id >= next ||
        r.size() > size - Trailer - offset)
      return false;
    prior = r.id;
    offset += r.size();
  }
  if (offset != size - Trailer || !file.seekSet(0)) return false;
  uint32_t checksum = ~0U;
  uint8_t buffer[128];
  for (size_t left = size - Trailer; left;) {
    const auto n = std::min(left, sizeof(buffer));
    if (file.read(buffer, n) != static_cast<int>(n)) return false;
    checksum = crc(checksum, buffer, n);
    left -= n;
  }
  uint8_t tail[4];
  return file.read(tail, 4) == 4 && get(tail, 4) == uint32_t(~checksum);
}
JournalResult LifecycleJournal::recover() {
  ready_ = false;
  selected_ = nullptr;
  count_ = 0;
  bytes_ = Header + Trailer;
  nextId_ = 1;
  if (!paths_.current || !paths_.staging || !paths_.backup || !limits_.entries || limits_.entries > MaxEntries ||
      limits_.bytes < Header + Trailer || limits_.bytes > MaxBytes)
    return error(JournalResult::Invalid);
  // An older writer must not fall back past a future generation it cannot read.
  for (const char* path : {paths_.current, paths_.backup}) {
    if (!Storage.exists(path)) continue;
    FsFile file = Storage.open(path, O_RDONLY);
    if (!file) return error(JournalResult::IoError);
    uint8_t h[8];
    const auto n = file.read(h, sizeof(h));
    const bool closed = close(file);
    if (n < 0 || !closed) return error(JournalResult::IoError);
    if (n == sizeof(h) && memcmp(h, "BSLJ", 4) == 0 && get(h + 4, 4) != Version)
      return error(JournalResult::RecoveryRequired);
  }
  if (Storage.exists(paths_.current)) {
    FsFile file = Storage.open(paths_.current, O_RDONLY);
    const bool valid = file && validate(file, nullptr);
    if (!close(file) || !valid) return error(JournalResult::RecoveryRequired);
  }
  const auto selected = FilePublish::select(paths_, validate);
  if (selected == PublishedFile::Unreadable || (Storage.exists(paths_.current) && selected != PublishedFile::Current))
    return error(JournalResult::RecoveryRequired);
  if (selected != PublishedFile::Missing) {
    selected_ = selected == PublishedFile::Current ? paths_.current : paths_.backup;
    FsFile file = Storage.open(selected_, O_RDONLY);
    uint8_t h[Header];
    const bool read = file && file.read(h, sizeof(h)) == sizeof(h);
    if (!close(file) || !read) return error(JournalResult::IoError);
    nextId_ = get(h + 8, 8);
    count_ = get(h + 16, 4);
    bytes_ = get(h + 20, 4);
    if (count_ > limits_.entries || bytes_ > limits_.bytes) return error(JournalResult::RecoveryRequired);
  }
  ready_ = true;
  return JournalResult::Ok;
}
JournalResult LifecycleJournal::enqueue(const JournalEntry& entry, uint64_t& id) {
  if (!validEntry(entry)) return error(JournalResult::Invalid);
  const auto result = change(Change::Append, nextId_, entry, 0);
  if (result == JournalResult::Ok) id = nextId_ - 1;
  return result;
}
JournalResult LifecycleJournal::checkpoint(uint64_t id, std::string_view bytes) {
  JournalEntry e;
  e.checkpoint = bytes;
  return change(Change::Checkpoint, id, e, 0);
}
JournalResult LifecycleJournal::acknowledge(uint64_t id, LifecyclePhase phase, std::string_view bytes) {
  if (unsigned(phase) > 4) return error(JournalResult::Invalid);
  JournalEntry e;
  e.checkpoint = bytes;
  return change(Change::Acknowledge, id, e, uint8_t(1U << unsigned(phase)));
}
JournalResult LifecycleJournal::finish(uint64_t id) { return change(Change::Finish, id, {}, 0); }
JournalResult LifecycleJournal::change(Change kind, uint64_t id, const JournalEntry& entry, uint8_t bit) {
  if (!ready_) return error(JournalResult::RecoveryRequired);
  if (entry.checkpoint.size() > MaxCheckpoint) return error(JournalResult::Invalid);
  uint32_t target = 0, targetSize = 0;
  uint16_t newCount = count_;
  uint32_t newBytes = bytes_;
  if (kind == Change::Append) {
    targetSize = info(entry, id).size();
    if (count_ >= limits_.entries || targetSize > limits_.bytes - bytes_ ||
        nextId_ == std::numeric_limits<uint64_t>::max())
      return error(JournalResult::Capacity);
    newBytes += targetSize;
    ++newCount;
  } else {
    if (!count_) return error(JournalResult::NotFound);
    FsFile file = selected_ ? Storage.open(selected_, O_RDONLY) : FsFile{};
    uint32_t offset = Header;
    bool found = false, ok = bool(file);
    RecordInfo r{};
    for (uint16_t i = 0; ok && i < count_; ++i) {
      ok = file.seekSet(offset) && record(file, r);
      if (ok && r.id == id) {
        target = offset;
        targetSize = r.size();
        found = true;
        break;
      }
      offset += r.size();
    }
    if (!close(file) || !ok) {
      ready_ = false;
      return error(JournalResult::IoError);
    }
    if (!found) return error(JournalResult::NotFound);
    if (kind == Change::Finish) {
      if (r.ack != AllPhases) return error(JournalResult::Incomplete);
      newBytes -= targetSize;
      --newCount;
    }
  }
  // From this point a false publication result has an uncertain local outcome.
  // Require explicit read-only recovery before any retry; never invent an ack.
  ready_ = false;
  FilePublish writer(paths_, validate);
  if (!writer.begin(newBytes)) return error(JournalResult::IoError);
  Output output(writer);
  uint8_t h[Header]{};
  memcpy(h, "BSLJ", 4);
  put(h + 4, Version, 4);
  put(h + 8, nextId_ + (kind == Change::Append), 8);
  put(h + 16, newCount, 4);
  put(h + 20, newBytes, 4);
  bool ok = output.write(h, sizeof(h));
  FsFile source;
  if (selected_) {
    source = Storage.open(selected_, O_RDONLY);
    ok = ok && source && source.seekSet(Header);
  }
  if (ok && kind == Change::Append) {
    if (source) ok = output.copy(source, bytes_ - Header - Trailer);
    const auto r = info(entry, id);
    encode(r, h);
    ok = ok && output.write(h, Record);
    for (const auto part : {entry.adapter, entry.owner, entry.document, entry.payload, entry.checkpoint})
      ok = ok && output.write(part.data(), part.size());
    ok = ok && output.zeros(MaxCheckpoint - entry.checkpoint.size());
  } else if (ok) {
    ok = output.copy(source, target - Header);
    if (kind != Change::Finish) {
      RecordInfo r{};
      ok = ok && record(source, r);
      r.lengths[4] = entry.checkpoint.size();
      r.ack |= bit;
      encode(r, h);
      ok = ok && output.write(h, Record) && output.copy(source, r.prefix()) &&
           output.write(entry.checkpoint.data(), entry.checkpoint.size()) &&
           output.zeros(MaxCheckpoint - entry.checkpoint.size());
    }
    ok = ok && source.seekSet(target + targetSize) && output.copy(source, bytes_ - Trailer - target - targetSize);
  }
  const bool closed = close(source);
  if (!ok || !closed || !output.finish() || !writer.finish()) return error(JournalResult::IoError);
  selected_ = paths_.current;
  nextId_ += (kind == Change::Append);
  bytes_ = newBytes;
  count_ = newCount;
  ready_ = true;
  return JournalResult::Ok;
}
JournalResult LifecycleJournal::read(uint64_t id, JournalEntry& entry, char* buffer, size_t capacity) {
  if (!ready_) return error(JournalResult::RecoveryRequired);
  if (!selected_) return JournalResult::NotFound;
  FsFile file = Storage.open(selected_, O_RDONLY);
  uint32_t offset = Header;
  bool ok = bool(file);
  JournalResult result = JournalResult::NotFound;
  for (uint16_t i = 0; ok && i < count_; ++i) {
    RecordInfo r{};
    ok = file.seekSet(offset) && record(file, r);
    if (ok && r.id == id) {
      const auto n = r.prefix() + r.lengths[4];
      if (!buffer || capacity < n) {
        result = JournalResult::Capacity;
        break;
      }
      ok = file.read(buffer, n) == static_cast<int>(n);
      if (ok) {
        JournalEntry out;
        out.id = r.id;
        out.acknowledged = r.ack;
        std::string_view* parts[] = {&out.adapter, &out.owner, &out.document, &out.payload, &out.checkpoint};
        size_t start = 0;
        for (int j = 0; j < 5; ++j) {
          *parts[j] = {buffer + start, r.lengths[j]};
          start += r.lengths[j];
        }
        entry = out;
        result = JournalResult::Ok;
      }
      break;
    }
    offset += r.size();
  }
  if (!close(file) || !ok) return error(JournalResult::IoError);
  return result;
}
JournalResult LifecycleJournal::next(std::string_view adapter, std::string_view owner, uint64_t after, uint64_t& id) {
  if (!ready_) return error(JournalResult::RecoveryRequired);
  if (!selected_) return JournalResult::NotFound;
  FsFile file = Storage.open(selected_, O_RDONLY);
  uint32_t offset = Header;
  bool ok = bool(file);
  JournalResult result = JournalResult::NotFound;
  for (uint16_t i = 0; ok && i < count_; ++i) {
    RecordInfo r{};
    ok = file.seekSet(offset) && record(file, r);
    if (ok && r.id > after && r.lengths[0] == adapter.size() && r.lengths[1] == owner.size()) {
      char buffer[MaxIdentity];
      ok = file.read(buffer, adapter.size()) == static_cast<int>(adapter.size());
      const bool match = ok && memcmp(buffer, adapter.data(), adapter.size()) == 0;
      ok = ok && file.read(buffer, owner.size()) == static_cast<int>(owner.size());
      if (ok && match && memcmp(buffer, owner.data(), owner.size()) == 0) {
        id = r.id;
        result = JournalResult::Ok;
        break;
      }
    }
    offset += r.size();
  }
  if (!close(file) || !ok) return error(JournalResult::IoError);
  return result;
}
}  // namespace booksync::storage
