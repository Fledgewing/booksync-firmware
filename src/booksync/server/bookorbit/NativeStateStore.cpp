// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeStateStore.h"

#include <Logging.h>

#include <cstring>

namespace booksync::server {
namespace {
constexpr size_t Header = 130;
uint64_t get(const char* p, unsigned n) {
  uint64_t value = 0;
  for (unsigned i = 0; i < n; ++i) value |= uint64_t(uint8_t(p[i])) << (8 * i);
  return value;
}
void put(char* p, uint64_t value, unsigned n) {
  for (unsigned i = 0; i < n; ++i) p[i] = char(value >> (8 * i));
}
uint32_t checksum(std::string_view bytes, uint32_t hash = 2166136261U) {
  for (const auto c : bytes) hash = (hash ^ uint8_t(c)) * 16777619U;
  return hash;
}
bool text(std::string_view value, size_t maximum, bool required = false) {
  return (!required || !value.empty()) && value.size() <= maximum && value.find('\0') == value.npos;
}
bool state(const StateValues& value, bool history) {
  return text(value.status, 32) && (!value.statusKnown || !value.status.empty()) &&
         text(value.statusModified, history ? 10 : 64) && value.rating <= 5 &&
         text(value.reviewNote, LifecycleState::MaxReviewBytes) && (value.reviewSet || value.reviewNote.empty()) &&
         (!value.reviewSet || value.reviewKnown);
}
uint8_t flags(const StateValues& value) {
  return value.statusKnown | value.ratingKnown << 1 | value.reviewKnown << 2 | value.reviewSet << 3;
}
bool sameState(const StateValues& a, const StateValues& b) {
  return flags(a) == flags(b) && a.status == b.status && a.statusModified == b.statusModified && a.rating == b.rating &&
         a.reviewNote == b.reviewNote;
}
bool same(const NativeStateDocument& a, const NativeStateDocument& b) {
  return a.scope == b.scope && a.source == b.source && a.token == b.token &&
         a.manualStatusPending == b.manualStatusPending && a.entry == b.entry && a.nativeOpen == b.nativeOpen &&
         a.payload == b.payload && a.document == b.document && a.reviewModified == b.reviewModified &&
         sameState(a.local, b.local) && sameState(a.history, b.history);
}
}  // namespace
bool NativeStateStore::overlaps(std::string_view value) const {
  if (value.empty()) return false;
  const auto at = reinterpret_cast<uintptr_t>(value.data()), begin = reinterpret_cast<uintptr_t>(scratch_);
  return (at >= begin && at - begin < capacity_) || (at < begin && begin - at < value.size());
}
bool NativeStateStore::disjoint(const char* bytes, size_t size) const {
  return !bytes || !size || !overlaps({bytes, size});
}
bool NativeStateStore::expect(storage::ScopeView scope, const storage::SourceIdentity& source) {
  if (!text(scope.server, 128, true) || !text(scope.account, 128, true) || !storage::valid(source)) return false;
  // The expected scope may borrow this store's previous decode workspace.
  // Copy before validation can overwrite those bytes with another document.
  // One 129-byte temporary also handles a scope returned by a previous Missing
  // load, including crossing the two member spans. Clearing first loses input.
  char server[129]{};
  std::memcpy(server, scope.server.data(), scope.server.size());
  std::memmove(account_.data(), scope.account.data(), scope.account.size());
  std::memset(account_.data() + scope.account.size(), 0, account_.size() - scope.account.size());
  std::memcpy(server_.data(), server, sizeof(server));
  expected_ = source;
  return true;
}
bool NativeStateStore::bound() const {
  return decoded_.scope.server == server_.data() && decoded_.scope.account == account_.data() &&
         decoded_.source == expected_;
}
bool NativeStateStore::valid(const NativeStateDocument& value) {
  if (!value.token || !text(value.scope.server, 128, true) || !text(value.scope.account, 128, true) ||
      !storage::valid(value.source) || !state(value.local, false) || !state(value.history, true) ||
      !text(value.reviewModified, 64))
    return false;
  if (!value.entry) return !value.nativeOpen && value.payload.empty() && value.document.empty();
  return value.nativeOpen && text(value.document, 128, true) && value.payload.size() <= 2512 &&
         reader::Capture::decodeOpen(value.payload, opened_) && opened_.bound && opened_.binding.scope == value.scope &&
         opened_.binding.source == value.source;
}
bool NativeStateStore::decode(std::string_view bytes) {
  if (bytes.size() < Header || bytes.size() > MaxBytes || bytes.substr(0, 4) != "BNS1" ||
      get(bytes.data() + 4, 4) != bytes.size() || get(bytes.data() + 8, 4) != checksum(bytes.substr(12)) ||
      (get(bytes.data() + 12, 2) & ~3U) || get(bytes.data() + 14, 2) || (uint8_t(bytes[124]) & ~15U) ||
      (uint8_t(bytes[126]) & ~15U))
    return false;
  decoded_ = {};
  const auto mode = get(bytes.data() + 12, 2);
  decoded_.manualStatusPending = mode & 1;
  decoded_.token = get(bytes.data() + 16, 8);
  decoded_.entry = get(bytes.data() + 24, 8);
  decoded_.nativeOpen = get(bytes.data() + 32, 8);
  std::memcpy(decoded_.source.book.bytes.data(), bytes.data() + 40, 16);
  std::memcpy(decoded_.source.edition.bytes.data(), bytes.data() + 56, 16);
  std::memcpy(decoded_.source.revision.bytes.data(), bytes.data() + 72, 32);
  size_t at = Header;
  const auto take = [&](size_t offset) -> std::string_view {
    const auto size = get(bytes.data() + offset, 2);
    if (at > bytes.size() || size > bytes.size() - at) {
      at = bytes.size() + 1;
      return {};
    }
    auto part = bytes.substr(at, size);
    at += size;
    return part;
  };
  decoded_.scope.server = take(104);
  decoded_.scope.account = take(106);
  decoded_.payload = take(108);
  decoded_.local.status = take(110);
  decoded_.local.statusModified = take(112);
  decoded_.local.reviewNote = take(114);
  decoded_.reviewModified = take(116);
  decoded_.history.status = take(118);
  decoded_.history.statusModified = take(120);
  decoded_.history.reviewNote = take(122);
  decoded_.document = take(128);
  decoded_.local.statusKnown = bytes[124] & 1;
  decoded_.local.ratingKnown = bytes[124] & 2;
  decoded_.local.reviewKnown = bytes[124] & 4;
  decoded_.local.reviewSet = bytes[124] & 8;
  decoded_.local.rating = uint8_t(bytes[125]);
  decoded_.history.statusKnown = bytes[126] & 1;
  decoded_.history.ratingKnown = bytes[126] & 2;
  decoded_.history.reviewKnown = bytes[126] & 4;
  decoded_.history.reviewSet = bytes[126] & 8;
  decoded_.history.rating = uint8_t(bytes[127]);
  return at == bytes.size() && bool(mode & 2) == bool(decoded_.entry) && valid(decoded_);
}
bool NativeStateStore::validateFile(FsFile& file, void* context) {
  auto& store = *static_cast<NativeStateStore*>(context);
  const auto size = file.fileSize();
  if (!store.scratch_ || store.capacity_ < MaxBytes || size < Header || size > MaxBytes ||
      file.read(store.scratch_, size) != static_cast<int>(size))
    return false;
  char extra;
  return file.read(&extra, 1) == 0 && store.decode({store.scratch_, size});
}
NativeStateLoad NativeStateStore::load(storage::ScopeView scope, const storage::SourceIdentity& source,
                                       NativeStateDocument& output) {
  if (!expect(scope, source)) return NativeStateLoad::Conflict;
  const auto selected = storage::FilePublish::select(paths_, validateFile, this);
  if (selected == storage::PublishedFile::Missing) {
    output = {};
    output.scope = {server_.data(), account_.data()};
    output.source = expected_;
    return NativeStateLoad::Missing;
  }
  if (selected == storage::PublishedFile::Unreadable || decoded_.entry ||
      (selected == storage::PublishedFile::Backup && Storage.exists(paths_.current)))
    return NativeStateLoad::Unreadable;
  if (!bound()) return NativeStateLoad::Conflict;
  output = decoded_;
  return NativeStateLoad::Ready;
}
bool NativeStateStore::loadCapture(storage::ScopeView scope, const storage::SourceIdentity& source, uint64_t entry,
                                   uint64_t nativeOpen, std::string_view payload, std::string_view document,
                                   NativeStateDocument& output) {
  if (!entry || !nativeOpen || overlaps(payload) || overlaps(document) || !expect(scope, source) ||
      !storage::FilePublish::validateCurrent(paths_, validateFile, this) || !bound() || decoded_.entry != entry ||
      decoded_.nativeOpen != nativeOpen || decoded_.payload != payload || decoded_.document != document) {
    LOG_ERR("BSS", "Frozen state unavailable or binding changed");
    return false;
  }
  output = decoded_;
  return true;
}
bool NativeStateStore::freeze(const NativeStateDocument& value) {
  if (!value.entry || !valid(value)) return false;
  for (const auto part : {value.scope.server, value.scope.account, value.payload, value.local.status,
                          value.local.statusModified, value.local.reviewNote, value.reviewModified,
                          value.history.status, value.history.statusModified, value.history.reviewNote, value.document})
    if (overlaps(part)) {
      LOG_ERR("BSS", "Frozen state input overlaps its workspace");
      return false;
    }
  if (Storage.exists(paths_.current) || Storage.exists(paths_.backup)) {
    storage::FilePublish confirm(paths_, validateFile, this);
    return storage::FilePublish::validateCurrent(paths_, validateFile, this) && same(value, decoded_) &&
           confirm.confirm();
  }
  return write(value, true);
}
bool NativeStateStore::save(const NativeStateDocument& value) {
  if (value.entry || !valid(value) || !expect(value.scope, value.source)) return false;
  const auto selected = storage::FilePublish::select(paths_, validateFile, this);
  if (selected == storage::PublishedFile::Unreadable ||
      (selected != storage::PublishedFile::Missing &&
       (!bound() || decoded_.entry || decoded_.token > value.token ||
        (selected == storage::PublishedFile::Backup && Storage.exists(paths_.current))))) {
    LOG_ERR("BSS", "Native state authority cannot be replaced");
    return false;
  }
  if (selected == storage::PublishedFile::Current && same(value, decoded_)) {
    storage::FilePublish confirm(paths_, validateFile, this);
    return confirm.confirm();
  }
  return write(value, false);
}
bool NativeStateStore::write(const NativeStateDocument& value, bool createOnly) {
  header_.fill(0);
  auto* header = header_.data();
  std::memcpy(header, "BNS1", 4);
  put(header + 12, value.manualStatusPending | bool(value.entry) << 1, 2);
  put(header + 16, value.token, 8);
  put(header + 24, value.entry, 8);
  put(header + 32, value.nativeOpen, 8);
  std::memcpy(header + 40, value.source.book.bytes.data(), 16);
  std::memcpy(header + 56, value.source.edition.bytes.data(), 16);
  std::memcpy(header + 72, value.source.revision.bytes.data(), 32);
  header[124] = char(flags(value.local));
  header[125] = char(value.local.rating);
  header[126] = char(flags(value.history));
  header[127] = char(value.history.rating);
  size_t size = Header, offset = 104;
  for (const auto part : {value.scope.server, value.scope.account, value.payload, value.local.status,
                          value.local.statusModified, value.local.reviewNote, value.reviewModified,
                          value.history.status, value.history.statusModified, value.history.reviewNote}) {
    put(header + offset, part.size(), 2);
    offset += 2;
    size += part.size();
  }
  put(header + 128, value.document.size(), 2);
  size += value.document.size();
  put(header + 4, size, 4);
  auto sum = checksum({header + 12, Header - 12});
  for (const auto part : {value.scope.server, value.scope.account, value.payload, value.local.status,
                          value.local.statusModified, value.local.reviewNote, value.reviewModified,
                          value.history.status, value.history.statusModified, value.history.reviewNote, value.document})
    sum = checksum(part, sum);
  put(header + 8, sum, 4);
  storage::FilePublish writer(paths_, validateFile, this);
  if (size > MaxBytes || !writer.begin(size) || !writer.write(header, Header)) return false;
  for (const auto part : {value.scope.server, value.scope.account, value.payload, value.local.status,
                          value.local.statusModified, value.local.reviewNote, value.reviewModified,
                          value.history.status, value.history.statusModified, value.history.reviewNote, value.document})
    if (!part.empty() && !writer.write(part.data(), part.size())) return false;
  return createOnly ? writer.finishNew() : writer.finish();
}
}  // namespace booksync::server
