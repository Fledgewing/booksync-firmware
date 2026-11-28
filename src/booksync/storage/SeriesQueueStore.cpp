// SPDX-License-Identifier: GPL-3.0-or-later
#include "SeriesQueueStore.h"

#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
namespace booksync::storage {
namespace {
constexpr uint32_t Basis = 2166136261u;
constexpr uint8_t IndexMagic[8] = {'B', 'S', 'Q', 'I', 1, 0, 0, 0};
constexpr uint8_t RecordMagic[8] = {'B', 'S', 'Q', 'R', 1, 0, 0, 0};
uint32_t queueHash(uint32_t h, const void* p, size_t n) {
  const auto* bytes = static_cast<const uint8_t*>(p);
  while (n--) h = (h ^ *bytes++) * 16777619u;
  return h;
}
template <size_t N>
bool queueText(const std::array<char, N>& s, bool required = false) {
  return (!required || s[0]) && std::memchr(s.data(), 0, N);
}
struct QueueCodec {
  FilePublish* writer = nullptr;
  FsFile* reader = nullptr;
  size_t bytes = 0;
  uint32_t hash = Basis;
  bool ok = true;
  FsFile* comparison = nullptr;
  void observe(const void* p, size_t n) {
    if (!ok) return;
    if (comparison) {
      const auto* expected = static_cast<const uint8_t*>(p);
      uint8_t actual[8]{};
      for (size_t offset = 0; offset < n;) {
        const auto count = std::min(n - offset, sizeof(actual));
        if (comparison->read(actual, count) != int(count) || std::memcmp(actual, expected + offset, count)) {
          ok = false;
          return;
        }
        offset += count;
      }
    }
    hash = queueHash(hash, p, n);
    bytes += n;
    if (writer) ok = writer->write(p, n);
  }
  void blob(void* p, size_t n) {
    if (!ok) return;
    if (reader) ok = reader->read(p, n) == int(n);
    observe(p, n);
  }
  template <typename T>
  void number(T& value, unsigned n) {
    uint8_t wire[8]{};
    if (!reader)
      for (unsigned i = 0; i < n; ++i) wire[i] = uint64_t(value) >> (8 * i);
    blob(wire, n);
    if (reader) {
      uint64_t decoded = 0;
      for (unsigned i = 0; i < n; ++i) decoded |= uint64_t(wire[i]) << (8 * i);
      value = static_cast<T>(decoded);
    }
  }
  void queueBoolean(bool& value) {
    uint8_t wire = value;
    number(wire, 1);
    if (wire > 1) ok = false;
    value = wire == 1;
  }
  template <size_t N>
  void text(std::array<char, N>& value) {
    if (reader) {
      blob(value.data(), N);
      ok = queueText(value) && ok;
      return;
    }
    const auto* end = static_cast<const char*>(std::memchr(value.data(), 0, N));
    if (!end) {
      ok = false;
      return;
    }
    const auto length = size_t(end - value.data());
    blob(value.data(), length);
    static constexpr uint8_t Zeros[64]{};
    size_t remaining = N - length;
    while (remaining) {
      const size_t n = remaining < sizeof(Zeros) ? remaining : sizeof(Zeros);
      observe(Zeros, n);
      remaining -= n;
    }
  }
  void key(BookKey& key) {
    number(key.provenance.generation, 4);
    number(key.provenance.accountEpoch, 4);
    number(key.provenance.owner, 8);
    number(key.provenance.capture, 8);
    number(key.ordinal, 4);
  }
  void request(SeriesRequest& r) {
    text(r.key);
    key(r.current);
    key(r.start);
    number(r.requestId, 8);
    number(r.count, 1);
    queueBoolean(r.currentKnown);
    queueBoolean(r.startKnown);
  }
  void spec(DownloadSpec& s) {
    blob(s.identity.book.bytes.data(), 16);
    blob(s.identity.edition.bytes.data(), 16);
    blob(s.identity.revision.bytes.data(), 32);
    text(s.server);
    text(s.account);
    text(s.book);
    text(s.file);
    text(s.variant);
    text(s.version);
    text(s.proof);
    text(s.path);
    number(s.bytes, 8);
    queueBoolean(s.sizeKnown);
  }
  void record(SeriesQueueRecord& r) {
    number(r.id, 8);
    number(r.sequence, 8);
    number(r.token, 8);
    request(r.request);
    number(r.count, 1);
    if (r.count < 1 || r.count > SeriesNextMax) {
      ok = false;
      return;
    }
    for (uint8_t i = 0; i < r.count; ++i) {
      auto& row = r.books[i];
      spec(row.file);
      key(row.key);
      text(row.title);
      text(row.destination);
      text(row.variantLabel);
      number(row.job, 8);
      number(row.state, 1);
      number(row.error, 1);
      queueBoolean(row.cancelWanted);
    }
  }
  void index(SeriesQueueIndex& r) {
    number(r.sequence, 8);
    number(r.counter, 8);
    for (auto& slot : r.slots) {
      number(slot.id, 8);
      text(slot.series);
      number(slot.cancelMask, 2);
      queueBoolean(slot.terminal);
    }
  }
};
void clearQueueSpec(DownloadSpec& s) {
  s.identity = {};
  s.server.fill(0);
  s.account.fill(0);
  s.book.fill(0);
  s.file.fill(0);
  s.variant.fill(0);
  s.version.fill(0);
  s.proof.fill(0);
  s.path.fill(0);
  s.bytes = 0;
  s.sizeKnown = false;
}
}  // namespace
void SeriesQueueBook::clear() {
  clearQueueSpec(file);
  key = {};
  title.fill(0);
  destination.fill(0);
  variantLabel.fill(0);
  job = 0;
  state = SeriesRowState::Queued;
  error = DownloadError::None;
  cancelWanted = false;
}
void SeriesQueueRecord::clear() {
  for (auto& book : books) book.clear();
  request = {};
  id = sequence = token = 0;
  count = 0;
}
void SeriesQueueIndex::clear() {
  for (auto& slot : slots) {
    slot.series.fill(0);
    slot.id = 0;
    slot.cancelMask = 0;
    slot.terminal = false;
  }
  sequence = counter = 0;
}
SeriesQueueStore::SeriesQueueStore(PublishPaths paths, const char* prefix, ScopeView scope) : indexPaths_(paths) {
  if (!prefix || !prefix[0] || std::strlen(prefix) >= prefix_.size() || scope.server.empty() || scope.account.empty() ||
      scope.server.size() >= server_.size() || scope.account.size() >= account_.size() ||
      scope.server.find('\0') != scope.server.npos || scope.account.find('\0') != scope.account.npos)
    return;
  std::memcpy(server_.data(), scope.server.data(), scope.server.size());
  std::memcpy(account_.data(), scope.account.data(), scope.account.size());
  std::strcpy(prefix_.data(), prefix);
  scopeValid_ = true;
}
bool SeriesQueueStore::setSlot(uint8_t slot) {
  if (slot >= 4 || !scopeValid_) return false;
  std::snprintf(paths_[0].data(), paths_[0].size(), "%s.%u", prefix_.data(), unsigned(slot));
  std::snprintf(paths_[1].data(), paths_[1].size(), "%s.%u.tmp", prefix_.data(), unsigned(slot));
  std::snprintf(paths_[2].data(), paths_[2].size(), "%s.%u.bak", prefix_.data(), unsigned(slot));
  return true;
}
bool SeriesQueueStore::scopeMatches(const DownloadSpec& s) const {
  return queueText(s.server, true) && queueText(s.account, true) && std::strcmp(s.server.data(), server_.data()) == 0 &&
         std::strcmp(s.account.data(), account_.data()) == 0;
}
bool SeriesQueueStore::validRecord(const SeriesQueueRecord& r) const {
  if (!r.id || !r.sequence || !r.token || r.count < 1 || r.count > SeriesNextMax || r.request.count < 1 ||
      r.request.count > SeriesNextMax || r.count > r.request.count || !queueText(r.request.key, true))
    return false;
  for (uint8_t i = 0; i < r.count; ++i) {
    const auto& row = r.books[i];
    if (!queueText(row.title) || !queueText(row.destination) || !queueText(row.variantLabel) ||
        unsigned(row.state) > unsigned(SeriesRowState::Unsupported) ||
        unsigned(row.error) > unsigned(DownloadError::Digest))
      return false;
    if (row.state == SeriesRowState::Unsupported) continue;
    const auto& s = row.file;
    if (!scopeMatches(s) || !queueText(s.book, true) || !queueText(s.file, true) || !queueText(s.variant, true) ||
        !queueText(s.version) || !queueText(s.proof) || !queueText(s.path, true) || (s.sizeKnown && !s.bytes))
      return false;
  }
  return true;
}
bool SeriesQueueStore::validIndex(const SeriesQueueIndex& r) const {
  for (size_t i = 0; i < r.slots.size(); ++i) {
    const auto& slot = r.slots[i];
    if (!queueText(slot.series) || (slot.id && (!slot.series[0] || slot.id > r.counter)) || (slot.cancelMask > 1023) ||
        (!slot.id && (slot.series[0] || slot.terminal || slot.cancelMask)))
      return false;
    if (slot.id)
      for (size_t j = 0; j < i; ++j)
        if (r.slots[j].id == slot.id) return false;
  }
  return r.sequence || !r.counter;
}
bool SeriesQueueStore::validFile(FsFile& file, void* context) {
  auto& self = *static_cast<SeriesQueueStore*>(context);
  const auto size = file.fileSize();
  if (self.recordMode_) {
    if (size < RecordBase || (size - RecordBase) % BookBytes || (size - RecordBase) / BookBytes > SeriesNextMax ||
        size == RecordBase)
      return false;
  } else if (size != IndexBytes)
    return false;
  uint8_t buffer[128];
  uint32_t hash = Basis;
  size_t at = 0;
  while (at < size - 4) {
    const size_t n = (size - 4 - at) < sizeof(buffer) ? size - 4 - at : sizeof(buffer);
    if (file.read(buffer, n) != int(n)) return false;
    for (size_t i = 0; i < n; ++i) {
      const size_t offset = at + i;
      if (offset < 8 && buffer[i] != (self.recordMode_ ? RecordMagic : IndexMagic)[offset]) return false;
      if (offset >= 8 && offset < 72 && buffer[i] != uint8_t(self.server_[offset - 8])) return false;
      if (offset >= 72 && offset < 136 && buffer[i] != uint8_t(self.account_[offset - 72])) return false;
      if (self.recordMode_ && self.expectedId_ && offset >= 136 && offset < 144 &&
          buffer[i] != uint8_t(self.expectedId_ >> (8 * (offset - 136))))
        return false;
      if (self.recordMode_ && offset == 291 && buffer[i] != (size - RecordBase) / BookBytes) return false;
    }
    hash = queueHash(hash, buffer, n);
    at += n;
  }
  uint8_t stored[4];
  if (file.read(stored, 4) != 4) return false;
  uint32_t expected = 0;
  for (unsigned i = 0; i < 4; ++i) expected |= uint32_t(stored[i]) << (8 * i);
  return hash == expected;
}
bool SeriesQueueStore::loadFile(PublishPaths paths, SeriesQueueIndex* index, SeriesQueueRecord* record) {
  if (!scopeValid_) return false;
  // An existing damaged/future/current record is retained and blocks mutation.
  // Missing current may recover its proven backup after a rename cut.
  if (Storage.exists(paths.current) && !FilePublish::validateCurrent(paths, validFile, this)) {
    // Slot reuse can leave a complete unreferenced newer record. Only a terminal
    // old slot is eligible for that case; caller selects its exact ID via backup.
    const auto id = expectedId_;
    expectedId_ = 0;
    const bool complete = FilePublish::validateCurrent(paths, validFile, this);
    expectedId_ = id;
    if (!complete) {
      LOG_ERR("BSQ", "Damaged or foreign Series queue retained");
      return false;
    }
  }
  const auto selected = FilePublish::select(paths, validFile, this);
  if (selected == PublishedFile::Missing) {
    if (index) index->clear();
    return index != nullptr;
  }
  if (selected == PublishedFile::Unreadable) return false;
  FsFile file = Storage.open(selected == PublishedFile::Current ? paths.current : paths.backup, O_RDONLY);
  if (!file) return false;
  bool ok = file.seekSet(136);
  QueueCodec codec{nullptr, &file};
  if (index) {
    index->clear();
    codec.index(*index);
  } else {
    record->clear();
    codec.record(*record);
  }
  ok = codec.ok && ok;
  const bool closed = file.close();
  return closed && ok && (index ? validIndex(*index) : validRecord(*record));
}
// Reconcile exact intended bytes, including sequence and normalized strings.
// Checksums detect damage; they never identify a durable acknowledged mutation.
__attribute__((noinline)) bool SeriesQueueStore::confirmFile(FilePublish& writer, PublishPaths paths, size_t length,
                                                             SeriesQueueIndex* index, SeriesQueueRecord* record) {
  if (!writer.confirm()) return false;
  FsFile file = Storage.open(paths.current, O_RDONLY);
  if (!file) return false;
  bool saved = file.fileSize() == length;
  QueueCodec codec;
  codec.comparison = &file;
  codec.observe(recordMode_ ? RecordMagic : IndexMagic, 8);
  codec.text(server_);
  codec.text(account_);
  if (index)
    codec.index(*index);
  else
    codec.record(*record);
  const auto hash = codec.hash;
  uint8_t tail[4];
  for (unsigned i = 0; i < 4; ++i) tail[i] = hash >> (8 * i);
  codec.observe(tail, 4);
  saved = codec.ok && codec.bytes == length && saved;
  return file.close() && saved;
}
bool SeriesQueueStore::saveFile(PublishPaths paths, SeriesQueueIndex* index, SeriesQueueRecord* record) {
  if (!scopeValid_ || !(index ? validIndex(*index) : validRecord(*record))) {
    LOG_ERR("BSQ", "Invalid Series queue publication");
    return false;
  }
  const auto wanted = expectedId_;
  expectedId_ = 0;
  if (Storage.exists(paths.current) && !FilePublish::validateCurrent(paths, validFile, this)) {
    expectedId_ = wanted;
    LOG_ERR("BSQ", "Unreadable Series queue blocks replacement");
    return false;
  }
  const size_t length = index ? IndexBytes : RecordBase + BookBytes * record->count;
  FilePublish writer(paths, validFile, this);
  if (!writer.begin(length)) {
    expectedId_ = wanted;
    return false;
  }
  QueueCodec codec{&writer};
  auto* magic = const_cast<uint8_t*>(recordMode_ ? RecordMagic : IndexMagic);
  codec.blob(magic, 8);
  codec.text(server_);
  codec.text(account_);
  if (index)
    codec.index(*index);
  else
    codec.record(*record);
  const auto hash = codec.hash;
  uint8_t tail[4];
  for (unsigned i = 0; i < 4; ++i) tail[i] = hash >> (8 * i);
  const bool written = codec.ok && codec.bytes + 4 == length && writer.write(tail, 4);
  bool saved = written && writer.finish();
  if (!saved && written) saved = confirmFile(writer, paths, length, index, record);
  expectedId_ = wanted;
  if (!saved) LOG_ERR("BSQ", "Series queue checkpoint failed");
  return saved;
}
bool SeriesQueueStore::recover(SeriesQueueIndex& out) {
  recordMode_ = false;
  expectedId_ = 0;
  return loadFile(indexPaths_, &out, nullptr);
}
bool SeriesQueueStore::saveIndex(SeriesQueueIndex& index) {
  recordMode_ = false;
  expectedId_ = 0;
  return saveFile(indexPaths_, &index, nullptr);
}
bool SeriesQueueStore::load(uint8_t slot, uint64_t id, SeriesQueueRecord& out) {
  if (!id || !setSlot(slot)) return false;
  recordMode_ = true;
  expectedId_ = id;
  return loadFile(recordPaths(), nullptr, &out);
}
bool SeriesQueueStore::save(uint8_t slot, SeriesQueueRecord& record) {
  if (!setSlot(slot)) return false;
  recordMode_ = true;
  expectedId_ = record.id;
  return saveFile(recordPaths(), nullptr, &record);
}
bool SeriesQueueStore::project(uint8_t slot, uint64_t id, SeriesQueueView& out) {
  out.clear();
  out.availability = Availability::Unavailable;
  if (!id || !setSlot(slot)) return false;
  recordMode_ = true;
  expectedId_ = id;
  const auto paths = recordPaths();
  const auto selected = FilePublish::select(paths, validFile, this);
  if (selected != PublishedFile::Current && selected != PublishedFile::Backup) return false;
  FsFile file = Storage.open(selected == PublishedFile::Current ? paths.current : paths.backup, O_RDONLY);
  if (!file) return false;
  QueueCodec codec{nullptr, &file};
  bool ok = file.seekSet(136);
  uint64_t token = 0;
  codec.number(out.id, 8);
  codec.number(out.sequence, 8);
  codec.number(token, 8);
  codec.text(out.series);
  ok = file.seekSet(291) && ok;
  codec.number(out.count, 1);
  if (out.count < 1 || out.count > SeriesNextMax) ok = false;
  for (uint8_t i = 0; ok && i < out.count; ++i) {
    auto& row = out.items[i];
    const size_t base = 292 + BookBytes * i;
    // The fixed v1 descriptor stores size immediately before the public row.
    ok = file.seekSet(base + 753) && ok;
    codec.number(row.bytes, 8);
    codec.queueBoolean(row.sizeKnown);
    codec.key(row.key);
    codec.text(row.title);
    codec.text(row.destination);
    codec.text(row.variant);
    codec.number(row.job, 8);
    codec.number(row.state, 1);
    codec.number(row.error, 1);
    codec.queueBoolean(row.cancelWanted);
    if (unsigned(row.state) > unsigned(SeriesRowState::Unsupported) ||
        unsigned(row.error) > unsigned(DownloadError::Digest))
      ok = false;
  }
  ok = file.close() && codec.ok && ok;
  if (!ok) {
    out.clear();
    out.availability = Availability::Unavailable;
    LOG_ERR("BSQ", "Series queue projection failed");
    return false;
  }
  out.availability = Availability::Ready;
  return true;
}
}  // namespace booksync::storage
