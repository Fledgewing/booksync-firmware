// SPDX-License-Identifier: GPL-3.0-or-later
#include "PositionStore.h"

#include <Logging.h>

#include <cmath>
#include <cstring>
#include <limits>

namespace booksync::storage {
namespace {
constexpr size_t Header = 16;
constexpr uint32_t Basis = 2166136261U;
uint32_t hash(uint32_t h, const char* p, size_t n) {
  for (size_t i = 0; i < n; ++i) h = (h ^ static_cast<uint8_t>(p[i])) * 16777619U;
  return h;
}
uint64_t get(const char* p, unsigned n) {
  uint64_t v = 0;
  for (unsigned i = 0; i < n; ++i) v |= uint64_t(static_cast<uint8_t>(p[i])) << (8 * i);
  return v;
}
void put(char* p, uint64_t v, unsigned n) {
  for (unsigned i = 0; i < n; ++i) p[i] = static_cast<char>(v >> (8 * i));
}
bool text(std::string_view v, size_t max) { return v.size() <= max && v.find('\0') == v.npos; }
bool validValue(const PositionValue& v) {
  return v.raw.size() <= PositionStore::MaxRaw && text(v.label, PositionStore::MaxLabel) &&
         std::isfinite(v.percentage) && v.percentage >= 0 && v.percentage <= 1 &&
         (v.percentagePresent || v.percentage == 0) &&
         (v.present || (!v.percentagePresent && !v.at && !v.kind && !v.version && v.raw.empty() && v.label.empty() &&
                        !v.reset && !v.redated)) &&
         (!v.reset || (v.percentagePresent && v.percentage == 0));
}
bool validRecord(const PositionRecord& r) {
  return valid(r.source) && !r.scope.server.empty() && !r.scope.account.empty() &&
         text(r.scope.server, PositionStore::MaxScope) && text(r.scope.account, PositionStore::MaxScope) &&
         validValue(r.remote) && validValue(r.marker) && validValue(r.overridePlace) &&
         (!r.answerable || (r.remote.present && r.token)) && (!r.pending || r.remote.present);
}
struct Encoder {
  FilePublish* writer = nullptr;
  size_t size = 0;
  uint32_t checksum = Basis;
  bool ok = true;
  void bytes(const char* p, size_t n) {
    size += n;
    checksum = hash(checksum, p, n);
    if (writer && n) ok = writer->write(p, n) && ok;
  }
  void number(uint64_t value, unsigned n) {
    char b[8];
    put(b, value, n);
    bytes(b, n);
  }
  void string(std::string_view v) {
    number(v.size(), 2);
    bytes(v.data(), v.size());
  }
  void value(const PositionValue& v) {
    number(v.present | v.percentagePresent << 1 | v.reset << 2 | v.redated << 3, 1);
    uint64_t bits;
    static_assert(sizeof(bits) == sizeof(v.percentage) && std::numeric_limits<double>::is_iec559);
    std::memcpy(&bits, &v.percentage, 8);
    number(bits, 8);
    number(v.at, 8);
    number(v.kind, 2);
    number(v.version, 2);
    string(v.raw);
    string(v.label);
  }
  void record(const PositionRecord& r) {
    string(r.scope.server);
    string(r.scope.account);
    bytes(reinterpret_cast<const char*>(r.source.book.bytes.data()), 16);
    bytes(reinterpret_cast<const char*>(r.source.edition.bytes.data()), 16);
    bytes(reinterpret_cast<const char*>(r.source.revision.bytes.data()), 32);
    number(r.token, 8);
    number(r.declinedAt, 8);
    number(r.rereadAt, 8);
    number(r.lastOverrideAt, 8);
    number(r.declinedReset | r.pending << 1 | r.answerable << 2 | r.allowPush << 3 | r.compared << 4 |
               r.rereadReleased << 5,
           1);
    value(r.remote);
    value(r.marker);
    value(r.overridePlace);
  }
};
struct Decoder {
  std::string_view bytes;
  size_t at = Header;
  bool ok = true;
  std::string_view take(size_t n) {
    if (at > bytes.size() || n > bytes.size() - at) {
      ok = false;
      return {};
    }
    auto result = bytes.substr(at, n);
    at += n;
    return result;
  }
  uint64_t number(unsigned n) {
    auto b = take(n);
    return b.size() == n ? get(b.data(), n) : 0;
  }
  std::string_view string() {
    const auto n = number(2);
    return take(n);
  }
  void value(PositionValue& v) {
    const auto flags = number(1);
    if (flags & ~15U) ok = false;
    v.present = flags & 1;
    v.percentagePresent = flags & 2;
    v.reset = flags & 4;
    v.redated = flags & 8;
    const uint64_t bits = number(8);
    std::memcpy(&v.percentage, &bits, 8);
    v.at = number(8);
    v.kind = number(2);
    v.version = number(2);
    v.raw = string();
    v.label = string();
  }
  bool record(PositionRecord& r) {
    r.scope.server = string();
    r.scope.account = string();
    for (auto* id : {&r.source.book.bytes, &r.source.edition.bytes}) {
      auto v = take(16);
      if (v.size() == 16) std::memcpy(id->data(), v.data(), 16);
    }
    auto rev = take(32);
    if (rev.size() == 32) std::memcpy(r.source.revision.bytes.data(), rev.data(), 32);
    r.token = number(8);
    r.declinedAt = number(8);
    r.rereadAt = number(8);
    r.lastOverrideAt = number(8);
    const auto flags = number(1);
    if (flags & ~63U) ok = false;
    r.declinedReset = flags & 1;
    r.pending = flags & 2;
    r.answerable = flags & 4;
    r.allowPush = flags & 8;
    r.compared = flags & 16;
    r.rereadReleased = flags & 32;
    value(r.remote);
    value(r.marker);
    value(r.overridePlace);
    return ok && at == bytes.size() && validRecord(r);
  }
};
// Validate semantic structure as well as integrity before a file can become
// publication authority. Reserved/new fields must not be erased by a v1 writer.
struct WireValidator {
  FsFile& file;
  size_t remaining;
  bool ok = true;
  uint64_t number(unsigned n) {
    char bytes[8];
    if (n > remaining || file.read(bytes, n) != static_cast<int>(n)) {
      ok = false;
      return 0;
    }
    remaining -= n;
    return get(bytes, n);
  }
  bool string(size_t max, bool binary = false, bool required = false) {
    const size_t n = number(2);
    if (n > max || n > remaining || (required && !n)) {
      ok = false;
      return false;
    }
    char block[128];
    for (size_t at = 0; at < n;) {
      const size_t count = n - at < sizeof(block) ? n - at : sizeof(block);
      if (file.read(block, count) != static_cast<int>(count)) {
        ok = false;
        return false;
      }
      remaining -= count;
      if (!binary && std::memchr(block, 0, count)) ok = false;
      at += count;
    }
    return n != 0;
  }
  bool value() {
    const auto flags = number(1), bits = number(8), at = number(8), kind = number(2), version = number(2);
    double percentage;
    std::memcpy(&percentage, &bits, 8);
    const bool raw = string(PositionStore::MaxRaw, true), label = string(PositionStore::MaxLabel);
    const bool present = flags & 1, fraction = flags & 2, reset = flags & 4;
    ok = ok && !(flags & ~15U) && std::isfinite(percentage) && percentage >= 0 && percentage <= 1 &&
         (fraction || percentage == 0) && (!reset || (fraction && percentage == 0)) &&
         (present || (!(flags & ~1U) && !at && !kind && !version && !raw && !label));
    return present;
  }
  bool record() {
    string(PositionStore::MaxScope, false, true);
    string(PositionStore::MaxScope, false, true);
    for (unsigned length : {16U, 16U, 32U}) {
      uint64_t nonzero = 0;
      for (unsigned i = 0; i < length; ++i) nonzero |= number(1);
      if (!nonzero) ok = false;
    }
    const auto token = number(8);
    number(8);
    number(8);
    number(8);
    const auto flags = number(1);
    const bool remote = value();
    value();
    value();
    return ok && !remaining && !(flags & ~63U) && (!(flags & 4) || (remote && token)) && (!(flags & 2) || remote);
  }
};
bool sameBinding(FsFile& f, ScopeView scope, const SourceIdentity& source) {
  if (!f.seekSet(Header)) return false;
  char small[2];
  for (auto expected : {scope.server, scope.account}) {
    if (f.read(small, 2) != 2 || get(small, 2) != expected.size()) return false;
    for (char c : expected)
      if (f.read(small, 1) != 1 || small[0] != c) return false;
  }
  for (auto expected : {std::string_view(reinterpret_cast<const char*>(source.book.bytes.data()), 16),
                        std::string_view(reinterpret_cast<const char*>(source.edition.bytes.data()), 16),
                        std::string_view(reinterpret_cast<const char*>(source.revision.bytes.data()), 32)})
    for (char c : expected)
      if (f.read(small, 1) != 1 || small[0] != c) return false;
  return true;
}
}  // namespace
bool PositionStore::validateFile(FsFile& file, void*) {
  char header[Header];
  if (file.read(header, Header) != Header || std::memcmp(header, "BPS1", 4) || get(header + 12, 4)) return false;
  const auto size = get(header + 4, 4);
#if SIZE_MAX < UINT64_MAX
  const auto actual = file.fileSize64();
#else
  const auto actual = file.fileSize();
#endif
  if (size <= Header || size > MaxBytes || size != actual) return false;
  char block[128];
  uint32_t checksum = Basis;
  for (size_t at = Header; at < size;) {
    const size_t n = size - at < sizeof(block) ? size - at : sizeof(block);
    if (file.read(block, n) != static_cast<int>(n)) return false;
    checksum = hash(checksum, block, n);
    at += n;
  }
  if (checksum != get(header + 8, 4) || !file.seekSet(Header)) return false;
  WireValidator validator{file, static_cast<size_t>(size - Header)};
  return validator.record();
}
PositionLoad PositionStore::load(ScopeView scope, const SourceIdentity& source, PositionRecord& out, char* scratch,
                                 size_t capacity) {
  if (!scratch || capacity < MaxBytes) {
    LOG_ERR("BSP", "Position read workspace unavailable");
    return PositionLoad::Unreadable;
  }
  const auto selected = FilePublish::select(paths_, validateFile);
  if (selected == PublishedFile::Missing) {
    const SourceIdentity identity = source;  // 64 bytes: protect aliased output on first load.
    out = {};
    out.scope = scope;
    out.source = identity;
    return PositionLoad::Missing;
  }
  if (selected == PublishedFile::Unreadable) return PositionLoad::Unreadable;
  auto file = Storage.open(selected == PublishedFile::Current ? paths_.current : paths_.backup, O_RDONLY);
  if (!file) {
    LOG_ERR("BSP", "Position open failed");
    return PositionLoad::Unreadable;
  }
  // Check before touching either output or scratch: expected scope/source may
  // themselves borrow the previous output. Decoding first would erase evidence
  // of a collision when the caller reuses its one worker buffer.
  if (!sameBinding(file, scope, source)) {
    LOG_ERR("BSP", "Position binding mismatch or read failure; bytes retained");
    const bool closed = file.close();
    return closed ? PositionLoad::Conflict : PositionLoad::Unreadable;
  }
  const size_t size = file.fileSize();
  const bool read = size <= capacity && file.seekSet(0) && file.read(scratch, size) == static_cast<int>(size);
  const bool closed = file.close();
  Decoder decoder{{scratch, read ? size : 0}};
  if (!read || !closed || !decoder.record(out)) {
    LOG_ERR("BSP", "Position decode failed");
    return PositionLoad::Unreadable;
  }
  return PositionLoad::Ready;
}
bool PositionStore::save(const PositionRecord& r) {
  if (!validRecord(r)) {
    LOG_ERR("BSP", "Position record invalid");
    return false;
  }
  const auto selected = FilePublish::select(paths_, validateFile);
  if (selected == PublishedFile::Unreadable) {
    LOG_ERR("BSP", "Position bytes unreadable; retained");
    return false;
  }
  // Reject scope/edition/path collisions rather than overwriting another book.
  if (selected != PublishedFile::Missing) {
    auto f = Storage.open(selected == PublishedFile::Current ? paths_.current : paths_.backup, O_RDONLY);
    if (!f) {
      LOG_ERR("BSP", "Position source open failed");
      return false;
    }
    const bool match = sameBinding(f, r.scope, r.source);
    const bool closed = f.close();
    if (!match || !closed) {
      LOG_ERR("BSP", "Position binding conflict");
      return false;
    }
    // Do not replace damaged primary bytes during backup recovery.
    if (selected == PublishedFile::Backup && Storage.exists(paths_.current)) return false;
  }
  Encoder measure;
  measure.record(r);
  if (measure.size + Header > MaxBytes) return false;
  char header[Header]{};
  std::memcpy(header, "BPS1", 4);
  put(header + 4, measure.size + Header, 4);
  put(header + 8, measure.checksum, 4);
  FilePublish writer(paths_, validateFile);
  if (!writer.begin(Header + measure.size) || !writer.write(header, Header)) return false;
  Encoder output{&writer};
  output.record(r);
  return output.ok && writer.finish();
}
}  // namespace booksync::storage
