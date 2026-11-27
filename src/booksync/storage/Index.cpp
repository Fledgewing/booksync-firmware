// SPDX-License-Identifier: GPL-3.0-or-later
#include "Index.h"

#include <Logging.h>

#include <cstring>
namespace booksync::storage {
namespace {
constexpr size_t HeaderBytes = 48;
void put(uint8_t* p, uint64_t n, size_t bytes) {
  for (size_t i = 0; i < bytes; ++i) p[i] = static_cast<uint8_t>(n >> (8 * i));
}
uint64_t get(const uint8_t* p, size_t bytes) {
  uint64_t n = 0;
  for (size_t i = 0; i < bytes; ++i) n |= uint64_t(p[i]) << (8 * i);
  return n;
}
uint32_t checksum(const uint8_t* p, size_t n) {
  uint32_t c = 2166136261u;
  for (size_t i = 0; i < n; ++i) c = (c ^ p[i]) * 16777619u;
  return c;
}
void encode(uint8_t* p, Provenance v) {
  put(p, v.generation, 4);
  put(p + 4, v.accountEpoch, 4);
  put(p + 8, v.owner, 8);
  put(p + 16, v.capture, 8);
}
Provenance decode(const uint8_t* p) {
  return {static_cast<uint32_t>(get(p, 4)), static_cast<uint32_t>(get(p + 4, 4)), get(p + 8, 8), get(p + 16, 8)};
}
bool text(const char* p, size_t n) { return p[0] && memchr(p, 0, n); }
bool path(const char* p, size_t n, bool catalog) { return memchr(p, 0, n) && (p[0] == '/' || (catalog && !p[0])); }
}  // namespace
Index::~Index() { cancel(); }
bool Index::fail(const char* step) {
  LOG_ERR("BSI", "Index failed: %s", step);
  failed_ = true;
  return false;
}
bool Index::validate(FsFile& f, void* context) {
  uint8_t h[HeaderBytes];
  const bool catalog = context && static_cast<Index*>(context)->catalogRecords_;
  const bool valid = f.fileSize() == HeaderBytes && f.read(h, sizeof(h)) == sizeof(h) &&
                     !memcmp(h, catalog ? "BSI2" : "BSI1", 4) && h[4] < 2 && !h[5] && !h[6] && !h[7] &&
                     get(h + 8, 4) > 0 && get(h + 12, 4) > 0 && get(h + 32, 4) <= MaxBooks &&
                     get(h + 36, 4) == RecordBytes && get(h + 40, 4) == 0 && get(h + 44, 4) == checksum(h, 44);
  if (!valid || !context) return valid;
  auto& index = *static_cast<Index*>(context);
  auto data = Storage.open(index.slots_[h[4]], O_RDONLY);
  uint8_t dataHeader[DataHeaderBytes]{};
  const bool complete = data && data.fileSize() == DataHeaderBytes + get(h + 32, 4) * RecordBytes &&
                        data.read(dataHeader, sizeof(dataHeader)) == sizeof(dataHeader) &&
                        !memcmp(dataHeader, catalog ? "BSD2" : "BSD1", 4) && !memcmp(dataHeader + 4, h + 8, 24) &&
                        get(dataHeader + 28, 4) == RecordBytes && get(dataHeader + 32, 4) == 0 &&
                        get(dataHeader + 36, 4) == checksum(dataHeader, 36);
  const bool closed = data.close();
  return complete && closed;
}
bool Index::recover() {
  if (active_) return fail("recover during scan");
  ready_ = false;
  provenance_ = {};
  count_ = 0;
  const auto selected = FilePublish::select(paths_, validate, this);
  if (selected == PublishedFile::Missing) {
    return true;
  }
  if (selected == PublishedFile::Unreadable) return fail("unreadable manifest retained");
  auto f = Storage.open(selected == PublishedFile::Current ? paths_.current : paths_.backup, O_RDONLY);
  uint8_t h[HeaderBytes]{};
  const bool read = f && f.read(h, sizeof(h)) == sizeof(h);
  const bool closed = f.close();
  if (!read || !closed) return fail("manifest read/close");
  // select() already validated header integrity and exact data-slot length.
  // The serialized worker excludes any mutation between selection and this read.
  provenance_ = decode(h + 8);
  count_ = get(h + 32, 4);
  slot_ = h[4];
  ready_ = true;
  failed_ = false;
  return true;
}

bool Index::begin(Provenance p) {
  if (active_) return fail("scan already active");
  // Re-select before reusing a data slot, preserving a valid fallback even if
  // the previously selected file was damaged since the last read.
  if (!recover()) return false;
  if (active_ || !p.generation || !p.accountEpoch ||
      (ready_ && (p.generation <= provenance_.generation || p.accountEpoch != provenance_.accountEpoch ||
                  p.owner != provenance_.owner)))
    return fail("generation/scope");
  pending_ = p;
  pendingCount_ = 0;
  pendingSlot_ = 1 - slot_;
  failed_ = false;
  writing_ = Storage.open(slots_[pendingSlot_], O_WRONLY | O_CREAT | O_TRUNC);
  if (!writing_) return fail("data open");
  active_ = true;
  uint8_t dataHeader[DataHeaderBytes]{};
  memcpy(dataHeader, catalogRecords_ ? "BSD2" : "BSD1", 4);
  encode(dataHeader + 4, pending_);
  put(dataHeader + 28, RecordBytes, 4);
  put(dataHeader + 36, checksum(dataHeader, 36), 4);
  if (writing_.write(dataHeader, sizeof(dataHeader)) != sizeof(dataHeader)) {
    fail("data header write");
    cancel();
    return false;
  }
  return true;
}
bool Index::append(const IndexRecord& r) {
  if (!active_ || failed_ || pendingCount_ >= MaxBooks || !path(r.path.data(), r.path.size(), catalogRecords_) ||
      !text(r.title.data(), r.title.size()))
    return fail("record bounds");
  auto* p = scratch_.data();
  scratch_.fill(0);
  memcpy(p, r.source.book.bytes.data(), 16);
  memcpy(p + 16, r.source.edition.bytes.data(), 16);
  memcpy(p + 32, r.source.revision.bytes.data(), 32);
  memcpy(p + 192, r.path.data(), r.path.size());
  memcpy(p + 64, r.title.data(), r.title.size());
  put(p + 160, r.page, 4);
  put(p + 164, r.pages, 4);
  put(p + 168, r.seconds, 4);
  p[172] = r.readingKnown;
  p[173] = r.titleTruncated;
  p[174] = r.statsKnown;
  put(p + 188, checksum(p, 188), 4);
  put(p + RecordBytes - 4, checksum(p + 192, RecordBytes - 196), 4);
  if (writing_.write(p, RecordBytes) != RecordBytes) return fail("data write");
  ++pendingCount_;
  return true;
}
void Index::cancel() {
  if (writing_ && !writing_.close()) LOG_ERR("BSI", "Index close failed");
  active_ = false;
}
void Index::retire() {
  cancel();
  ready_ = false;
  provenance_ = {};
  count_ = 0;
}
bool Index::commit() {
  if (!active_ || failed_) {
    cancel();
    return false;
  }
  const bool synced = writing_.sync();
  const bool closed = writing_.close();
  active_ = false;
  if (!synced || !closed) return fail("data sync/close");
  uint8_t h[HeaderBytes]{};
  memcpy(h, catalogRecords_ ? "BSI2" : "BSI1", 4);
  h[4] = pendingSlot_;
  encode(h + 8, pending_);
  put(h + 32, pendingCount_, 4);
  put(h + 36, RecordBytes, 4);
  put(h + 44, checksum(h, 44), 4);
  FilePublish publication(paths_, validate, this);
  if (!publication.begin(sizeof(h)) || !publication.write(h, sizeof(h)) || !publication.finish()) {
    // F06 can report a failed directory barrier after a complete new manifest
    // is already visible. Adopt the selected complete generation before retry.
    if (!recover()) LOG_ERR("BSI", "Cannot reconcile index after publication failure");
    return fail("manifest publish");
  }
  slot_ = pendingSlot_;
  provenance_ = pending_;
  count_ = pendingCount_;
  ready_ = true;
  return true;
}
bool Index::read(BookKey key, IndexRecord& r) {
  if (!ready_ || key.provenance != provenance_ || key.ordinal >= count_) return false;
  auto f = Storage.open(slots_[slot_], O_RDONLY);
  auto* p = scratch_.data();
  const bool read =
      f && f.seekSet(DataHeaderBytes + size_t(key.ordinal) * RecordBytes) && f.read(p, RecordBytes) == RecordBytes;
  const bool closed = f.close();
  if (!read || !closed || get(p + RecordBytes - 4, 4) != checksum(p + 192, RecordBytes - 196) ||
      !path(reinterpret_cast<char*>(p + 192), 1025, catalogRecords_) || !text(reinterpret_cast<char*>(p + 64), 96) ||
      (p[172] > 1 || p[173] > 1 || p[174] > 1) || get(p + 188, 4) != checksum(p, 188))
    return fail("record read/integrity");
  memcpy(r.source.book.bytes.data(), p, 16);
  memcpy(r.source.edition.bytes.data(), p + 16, 16);
  memcpy(r.source.revision.bytes.data(), p + 32, 32);
  memcpy(r.path.data(), p + 192, 1025);
  memcpy(r.title.data(), p + 64, 96);
  r.page = get(p + 160, 4);
  r.pages = get(p + 164, 4);
  r.seconds = get(p + 168, 4);
  r.readingKnown = p[172];
  r.titleTruncated = p[173];
  r.statsKnown = p[174];
  return true;
}
bool Index::summary(BookKey key, BookRow& row) {
  if (!ready_ || key.provenance != provenance_ || key.ordinal >= count_) return false;
  auto* p = scratch_.data();
  constexpr size_t SummaryBytes = 192;
  auto f = Storage.open(slots_[slot_], O_RDONLY);
  const bool read =
      f && f.seekSet(DataHeaderBytes + size_t(key.ordinal) * RecordBytes) && f.read(p, SummaryBytes) == SummaryBytes;
  const bool closed = f.close();
  if (!read || !closed || get(p + 188, 4) != checksum(p, 188) || !text(reinterpret_cast<char*>(p + 64), 96) ||
      (p[172] > 1 || p[173] > 1 || p[174] > 1))
    return fail("summary read/integrity");
  row = {};
  row.key = key;
  memcpy(row.book.data(), p, 16);
  memcpy(row.edition.data(), p + 16, 16);
  memcpy(row.revision.data(), p + 32, 32);
  memcpy(row.title.data(), p + 64, 96);
  // Use the F08 contract rather than deriving identity from paths or titles.
  const SourceIdentity evidence{{row.book}, {row.edition}, {row.revision}};
  row.identityKnown = valid(evidence);
  row.page = get(p + 160, 4);
  row.pages = get(p + 164, 4);
  row.seconds = get(p + 168, 4);
  row.readingKnown = p[172];
  row.statsKnown = p[174];
  row.titleTruncated = p[173];
  return true;
}
}  // namespace booksync::storage
