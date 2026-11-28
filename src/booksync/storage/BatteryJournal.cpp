// SPDX-License-Identifier: GPL-3.0-or-later
#include "BatteryJournal.h"

#include <Logging.h>

#include <cstdio>
#include <cstring>
namespace booksync::storage {
namespace {
constexpr uint32_t Header = 16;
void put(char* p, uint64_t n, unsigned size) {
  for (unsigned i = 0; i < size; ++i) p[i] = char(n >> (8 * i));
}
uint64_t get(const char* p, unsigned size) {
  uint64_t n = 0;
  for (unsigned i = 0; i < size; ++i) n |= uint64_t(uint8_t(p[i])) << (8 * i);
  return n;
}
uint32_t crc(const char* p, size_t size) {
  uint32_t n = ~0U;
  while (size--) {
    n ^= uint8_t(*p++);
    for (unsigned i = 0; i < 8; ++i) n = (n >> 1) ^ (0xedb88320U & (0U - (n & 1)));
  }
  return ~n;
}
bool valid(const BatteryRecord& e) {
  return e.kind >= battery::KIND_BOOT && e.kind <= battery::KIND_GAP && e.pct <= 100 && e.lightLevel <= 100 &&
         e.content() <= battery::CONTENT_XTC &&
         (e.kind == battery::KIND_BOOT ? e.resetClass() <= battery::RESET_CRASH : !e.resetClass());
}
void encode(const BatteryRecord& e, char* p) {
  battery::encode(e, reinterpret_cast<uint8_t*>(p));
  put(p + 16, crc(p, 16), 4);
}
bool decode(const char* p, BatteryRecord& e) {
  if (get(p + 16, 4) != crc(p, 16)) return false;
  e = battery::decode(reinterpret_cast<const uint8_t*>(p));
  return valid(e);
}
struct Segment {
  uint32_t number, rows = 0;
};
JournalResult error(JournalResult result) {
  LOG_ERR("BSBG", "Battery history retained; operation blocked: %u", unsigned(result));
  return result;
}
}  // namespace
bool BatteryJournal::paths(uint32_t segment) {
  const int n = snprintf(current_, sizeof(current_), "%s/%06lu.bin", directory_, static_cast<unsigned long>(segment));
  return n > 0 && size_t(n) < sizeof(current_) && snprintf(staging_, sizeof(staging_), "%s.tmp", current_) > 0 &&
         snprintf(backup_, sizeof(backup_), "%s.bak", current_) > 0;
}
bool BatteryJournal::validate(FsFile& file, void* context) {
  auto& segment = *static_cast<Segment*>(context);
  char bytes[RowBytes];
  if (file.read(bytes, Header) != Header || memcmp(bytes, "BSB1", 4) || get(bytes + 4, 4) != segment.number ||
      get(bytes + 12, 4))
    return false;
  const auto rows = get(bytes + 8, 4);
  if (!rows || rows > SegmentRows || file.fileSize() != Header + rows * RowBytes) return false;
  BatteryRecord e;
  for (unsigned i = 0; i < rows; ++i)
    if (file.read(bytes, RowBytes) != RowBytes || !decode(bytes, e)) return false;
  segment.rows = rows;
  return true;
}
PublishedFile BatteryJournal::select(uint32_t segment, uint32_t& rows) {
  if (!paths(segment)) return PublishedFile::Unreadable;
  // Match F07: a future version, or damaged committed current, must not be
  // replaced by an older readable backup. Preserve every unresolved source.
  for (const char* path : {current_, backup_}) {
    if (!Storage.exists(path)) continue;
    auto file = Storage.open(path, O_RDONLY);
    if (!file) return PublishedFile::Unreadable;
    char magic[4];
    const auto n = file.read(magic, sizeof(magic));
    const bool closed = file.close();
    if (n == 4 && !memcmp(magic, "BSB", 3) && magic[3] != '1') future_ = true;
    if (n < 0 || !closed || future_) return PublishedFile::Unreadable;
  }
  Segment context{segment};
  const auto selected = FilePublish::select({current_, staging_, backup_}, validate, &context);
  if (Storage.exists(current_) && selected != PublishedFile::Current) return PublishedFile::Unreadable;
  rows = context.rows;
  return selected;
}
JournalResult BatteryJournal::recover(bool create) {
  auto result = startRecovery(create);
  while (result == JournalResult::Incomplete) result = recoverStep();
  return result;
}
JournalResult BatteryJournal::startRecovery(bool create) {
  future_ = ready_ = capped_ = recovering_ = recoveryTail_ = false;
  count_ = recoverySegment_ = 0;
  if (!directory_ || directory_[0] != '/' || !limit_ || limit_ > MaxEvents) return error(JournalResult::Invalid);
  if (!Storage.exists(directory_)) {
    if (!create) return JournalResult::NotFound;
    if (!Storage.mkdir(directory_)) return error(JournalResult::IoError);
  }
  recovering_ = true;
  return JournalResult::Incomplete;
}
JournalResult BatteryJournal::recoverStep() {
  if (!recovering_) return error(JournalResult::Invalid);
  if (recoverySegment_ < (limit_ + SegmentRows) / SegmentRows) {
    const uint32_t i = recoverySegment_++;
    Segment s{i};
    const auto selected = select(i, s.rows);
    if (selected == PublishedFile::Unreadable ||
        (selected != PublishedFile::Missing && (recoveryTail_ || count_ + s.rows > limit_ + 1))) {
      recovering_ = false;
      return error(JournalResult::RecoveryRequired);
    }
    if (selected == PublishedFile::Missing)
      recoveryTail_ = true;
    else {
      count_ += s.rows;
      if (s.rows < SegmentRows) recoveryTail_ = true;
    }
    return JournalResult::Incomplete;
  }
  recovering_ = false;
  ready_ = true;
  if (count_) {
    BatteryRecord e;
    if (read(count_ - 1, e) != JournalResult::Ok || (count_ > limit_ && e.kind != battery::KIND_GAP)) {
      ready_ = false;
      return error(JournalResult::RecoveryRequired);
    }
    capped_ = count_ > limit_;
  }
  return JournalResult::Ok;
}
JournalResult BatteryJournal::read(uint32_t row, BatteryRecord& event) {
  if (!ready_ || row >= count_ || !paths(row / SegmentRows)) return error(JournalResult::Invalid);
  Segment s{row / SegmentRows};
  const auto selected = select(row / SegmentRows, s.rows);
  if ((selected != PublishedFile::Current && selected != PublishedFile::Backup) || row % SegmentRows >= s.rows)
    return error(JournalResult::RecoveryRequired);
  auto file = Storage.open(selected == PublishedFile::Current ? current_ : backup_, O_RDONLY);
  char bytes[RowBytes];
  bool ok = file && file.seekSet(Header + (row % SegmentRows) * RowBytes) && file.read(bytes, RowBytes) == RowBytes &&
            decode(bytes, event);
  const bool closed = file.close();
  return ok && closed ? JournalResult::Ok : error(JournalResult::IoError);
}
JournalResult BatteryJournal::append(const BatteryRecord& input) {
  if (!valid(input)) return error(JournalResult::Invalid);
  if (!ready_) {
    auto result = recover(true);
    if (result != JournalResult::Ok) return result;
  }
  if (capped_ && !uncertain_) return error(JournalResult::Capacity);
  BatteryRecord event = input;
  bool duplicate = false;
  if (count_) {
    BatteryRecord last;
    if (read(count_ - 1, last) != JournalResult::Ok) return error(JournalResult::RecoveryRequired);
    if (capped_) event = last;
    duplicate = last == event;
  }
  // One permanently reserved slot records an open-ended capacity gap. No old
  // row, synced or unsynced, is ever overwritten to admit a new observation.
  // An identical visible tail after a failed directory barrier is not a
  // durable acknowledgment. Republish that same bounded generation before
  // releasing the producer's copy, including an uncertain capacity marker.
  const bool cap = !duplicate && count_ == limit_;
  if (cap) {
    event = {};
    event.kind = battery::KIND_GAP;
    event.t = input.t;
    event.era = input.era;
    event.flags = input.flags & battery::FLAG_APPROX_TIME;
  }
  const uint32_t prefix = count_ - uint32_t(duplicate);
  const uint32_t segment = prefix / SegmentRows, rows = prefix % SegmentRows;
  const uint32_t existing = rows + uint32_t(duplicate);
  if (!paths(segment)) return error(JournalResult::Invalid);
  Segment s{segment};
  const auto selected = select(segment, s.rows);
  if ((existing && ((selected != PublishedFile::Current && selected != PublishedFile::Backup) || s.rows != existing)) ||
      (!existing && selected != PublishedFile::Missing))
    return error(JournalResult::RecoveryRequired);

  char bytes[RowBytes]{};
  memcpy(bytes, "BSB1", 4);
  put(bytes + 4, segment, 4);
  put(bytes + 8, rows + 1, 4);
  FilePublish writer({current_, staging_, backup_}, validate, &s);
  bool ok = writer.begin(Header + (rows + 1) * RowBytes) && writer.write(bytes, Header);
  auto source = rows && ok ? Storage.open(selected == PublishedFile::Current ? current_ : backup_, O_RDONLY) : FsFile{};
  if (rows) ok = ok && source && source.seekSet(Header);
  for (uint32_t i = 0; ok && i < rows; ++i)
    ok = source.read(bytes, RowBytes) == RowBytes && writer.write(bytes, RowBytes);
  const bool closed = source.close();
  encode(event, bytes);
  ok = ok && closed && writer.write(bytes, RowBytes) && writer.finish();
  if (!ok) {
    ready_ = false;
    uncertain_ = true;
    return error(JournalResult::IoError);
  }
  if (!duplicate) ++count_;
  capped_ = capped_ || cap;
  uncertain_ = false;
  return capped_ ? error(JournalResult::Capacity) : JournalResult::Ok;
}
}  // namespace booksync::storage
