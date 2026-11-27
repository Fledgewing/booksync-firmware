// SPDX-License-Identifier: GPL-3.0-or-later
#include "PageJournal.h"

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
bool valid(const PageEvent& e) {
  if (e.flags & ~(PageEvent::Gap | PageEvent::HasOffset)) return false;
  if (e.flags & PageEvent::Gap) return e.flags == PageEvent::Gap;
  return e.open && e.ordinal && e.start >= 946684800 && e.start <= 253402214400ULL && e.seconds && e.scale &&
         e.fraction <= e.scale && e.pages && e.page < e.pages;
}
void encode(const PageEvent& e, char* p) {
  memset(p, 0, PageJournal::RowBytes);
  put(p, e.open, 8);
  put(p + 8, e.start, 8);
  put(p + 16, e.ordinal, 4);
  put(p + 20, e.seconds, 4);
  put(p + 24, e.spine, 4);
  put(p + 28, e.page, 4);
  put(p + 32, e.pages, 4);
  put(p + 36, e.offset, 4);
  put(p + 40, e.fraction, 4);
  put(p + 44, e.scale, 4);
  p[48] = e.flags;
  put(p + 60, crc(p, 60), 4);
}
bool decode(const char* p, PageEvent& e) {
  if (get(p + 60, 4) != crc(p, 60)) return false;
  for (unsigned i = 49; i < 60; ++i)
    if (p[i]) return false;
  e = {get(p, 8),
       get(p + 8, 8),
       uint32_t(get(p + 16, 4)),
       uint32_t(get(p + 20, 4)),
       uint32_t(get(p + 24, 4)),
       uint32_t(get(p + 28, 4)),
       uint32_t(get(p + 32, 4)),
       uint32_t(get(p + 36, 4)),
       uint32_t(get(p + 40, 4)),
       uint32_t(get(p + 44, 4)),
       uint8_t(p[48])};
  return valid(e);
}
struct Segment {
  uint32_t number, rows = 0;
};
JournalResult error(JournalResult result) {
  LOG_ERR("BSPG", "Page history retained; operation blocked: %u", unsigned(result));
  return result;
}
}  // namespace
bool PageJournal::paths(uint32_t segment) {
  const int n = snprintf(current_, sizeof(current_), "%s/%06lu.bin", directory_, static_cast<unsigned long>(segment));
  return n > 0 && size_t(n) < sizeof(current_) && snprintf(staging_, sizeof(staging_), "%s.tmp", current_) > 0 &&
         snprintf(backup_, sizeof(backup_), "%s.bak", current_) > 0;
}
bool PageJournal::validate(FsFile& file, void* context) {
  auto& segment = *static_cast<Segment*>(context);
  char bytes[RowBytes];
  if (file.read(bytes, Header) != Header || memcmp(bytes, "BSP1", 4) || get(bytes + 4, 4) != segment.number ||
      get(bytes + 12, 4))
    return false;
  const auto rows = get(bytes + 8, 4);
  if (!rows || rows > SegmentRows || file.fileSize() != Header + rows * RowBytes) return false;
  PageEvent e;
  for (unsigned i = 0; i < rows; ++i)
    if (file.read(bytes, RowBytes) != RowBytes || !decode(bytes, e)) return false;
  segment.rows = rows;
  return true;
}
PublishedFile PageJournal::select(uint32_t segment, uint32_t& rows) {
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
    if (n < 0 || !closed || (n == 4 && !memcmp(magic, "BSP", 3) && magic[3] != '1')) return PublishedFile::Unreadable;
  }
  Segment context{segment};
  const auto selected = FilePublish::select({current_, staging_, backup_}, validate, &context);
  if (Storage.exists(current_) && selected != PublishedFile::Current) return PublishedFile::Unreadable;
  rows = context.rows;
  return selected;
}
JournalResult PageJournal::recover() {
  ready_ = false;
  count_ = 0;
  capped_ = false;
  if (!directory_ || directory_[0] != '/' || !limit_ || limit_ > MaxEvents) return error(JournalResult::Invalid);
  if (!Storage.exists(directory_) && !Storage.mkdir(directory_)) return error(JournalResult::IoError);
  bool tail = false;
  // Bounded discovery, including holes: do not silently abandon later segments.
  for (uint32_t i = 0; i < (limit_ + SegmentRows) / SegmentRows; ++i) {
    if (!paths(i)) return error(JournalResult::Invalid);
    Segment s{i};
    auto selected = select(i, s.rows);
    if (selected == PublishedFile::Unreadable) return error(JournalResult::RecoveryRequired);
    if (selected == PublishedFile::Missing) {
      tail = true;
      continue;
    }
    if (tail || count_ + s.rows > limit_ + 1) return error(JournalResult::RecoveryRequired);
    count_ += s.rows;
    if (s.rows < SegmentRows) tail = true;
  }
  ready_ = true;
  if (count_) {
    PageEvent e;
    if (read(count_ - 1, e) != JournalResult::Ok) {
      ready_ = false;
      return error(JournalResult::RecoveryRequired);
    }
    capped_ = count_ > limit_;
    if (capped_ && e.flags != PageEvent::Gap) {
      ready_ = false;
      return error(JournalResult::RecoveryRequired);
    }
  }
  return JournalResult::Ok;
}
JournalResult PageJournal::read(uint32_t row, PageEvent& event) {
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
JournalResult PageJournal::append(const PageEvent& input) {
  if (!valid(input)) return error(JournalResult::Invalid);
  if (!ready_) {
    auto result = recover();
    if (result != JournalResult::Ok) return result;
  }
  if (capped_) return error(JournalResult::Capacity);
  if (count_) {
    PageEvent last;
    if (read(count_ - 1, last) != JournalResult::Ok) return error(JournalResult::RecoveryRequired);
    if (last == input) return JournalResult::Ok;
  }
  // One permanently reserved slot records an open-ended capacity gap. No old
  // row, synced or unsynced, is ever overwritten to admit a new observation.
  PageEvent event = input;
  const bool cap = count_ == limit_;
  if (cap) event = {0, input.start, input.ordinal, 0, 0, 0, 0, 0, 0, 0, PageEvent::Gap};
  const uint32_t segment = count_ / SegmentRows, rows = count_ % SegmentRows;
  if (!paths(segment)) return error(JournalResult::Invalid);
  Segment s{segment};
  const auto selected = select(segment, s.rows);
  if ((rows && ((selected != PublishedFile::Current && selected != PublishedFile::Backup) || s.rows != rows)) ||
      (!rows && selected != PublishedFile::Missing))
    return error(JournalResult::RecoveryRequired);

  char bytes[RowBytes]{};
  memcpy(bytes, "BSP1", 4);
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
    return error(JournalResult::IoError);
  }
  ++count_;
  capped_ = cap;
  return cap ? error(JournalResult::Capacity) : JournalResult::Ok;
}
}  // namespace booksync::storage
