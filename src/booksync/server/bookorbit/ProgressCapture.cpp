// SPDX-License-Identifier: GPL-3.0-or-later
#include "ProgressCapture.h"

#include <Logging.h>

#include <cmath>
#include <cstring>
#include <limits>

namespace booksync::server {
namespace {
constexpr size_t Header = 126, MaxOpen = 2512;
uint64_t get(const char* p, unsigned n) {
  uint64_t value = 0;
  for (unsigned i = 0; i < n; ++i) value |= uint64_t(uint8_t(p[i])) << (8 * i);
  return value;
}
void put(char* p, uint64_t value, unsigned n) {
  for (unsigned i = 0; i < n; ++i) p[i] = char(value >> (8 * i));
}
uint32_t checksum(std::string_view bytes, uint32_t value = 2166136261U) {
  for (const auto c : bytes) value = (value ^ uint8_t(c)) * 16777619U;
  return value;
}
bool text(std::string_view value, size_t maximum, bool required = false) {
  return (!required || !value.empty()) && value.size() <= maximum && value.find('\0') == value.npos;
}
bool same(const FrozenProgress& a, const FrozenProgress& b) {
  return a.scope == b.scope && a.source == b.source && a.entry == b.entry && a.nativeOpen == b.nativeOpen &&
         a.payload == b.payload && a.document == b.document && a.value == b.value && a.deliver == b.deliver &&
         a.wasOverride == b.wasOverride;
}
}  // namespace
bool ProgressCapture::overlaps(std::string_view view) const {
  if (view.empty()) return false;
  const auto begin = reinterpret_cast<uintptr_t>(scratch_), at = reinterpret_cast<uintptr_t>(view.data());
  return (at >= begin && at - begin < capacity_) || (at < begin && begin - at < view.size());
}
bool ProgressCapture::valid(const FrozenProgress& value) {
  const auto& position = value.value;
  return value.entry && value.nativeOpen && storage::valid(value.source) && text(value.document, 128, true) &&
         text(value.scope.server, 128, true) && text(value.scope.account, 128, true) &&
         value.payload.size() <= MaxOpen && reader::Capture::decodeOpen(value.payload, opened_) && opened_.bound &&
         opened_.binding.scope == value.scope && opened_.binding.source == value.source &&
         position.raw.size() <= storage::PositionStore::MaxRaw &&
         text(position.label, storage::PositionStore::MaxLabel) && std::isfinite(position.percentage) &&
         position.percentage >= 0 && position.percentage <= 1 &&
         (position.percentagePresent || position.percentage == 0) &&
         (!position.reset || (position.percentagePresent && position.percentage == 0)) &&
         (position.present ||
          (!position.percentagePresent && !position.at && !position.kind && !position.version && position.raw.empty() &&
           position.label.empty() && !position.reset && !position.redated && !position.browser)) &&
         (!value.wasOverride || value.deliver) &&
         (!value.deliver || (position.present && position.percentagePresent && position.at && !position.raw.empty()));
}
bool ProgressCapture::decode(std::string_view bytes) {
  if (bytes.size() < Header || bytes.size() > MaxBytes || bytes.substr(0, 4) != "BPG1" ||
      get(bytes.data() + 4, 4) != bytes.size() || get(bytes.data() + 8, 4) != checksum(bytes.substr(12)))
    return false;
  const auto flags = get(bytes.data() + 12, 2);
  if (flags & ~127U) return false;
  size_t at = Header;
  const auto take = [&](size_t offset) -> std::string_view {
    const auto size = get(bytes.data() + offset, 2);
    if (at > bytes.size() || size > bytes.size() - at) {
      at = bytes.size() + 1;
      return {};
    }
    auto result = bytes.substr(at, size);
    at += size;
    return result;
  };
  decoded_ = {};
  decoded_.entry = get(bytes.data() + 28, 8);
  decoded_.nativeOpen = get(bytes.data() + 36, 8);
  decoded_.value.at = get(bytes.data() + 44, 8);
  const auto fraction = get(bytes.data() + 52, 8);
  static_assert(sizeof(fraction) == sizeof(decoded_.value.percentage) && std::numeric_limits<double>::is_iec559);
  std::memcpy(&decoded_.value.percentage, &fraction, 8);
  std::memcpy(decoded_.source.book.bytes.data(), bytes.data() + 60, 16);
  std::memcpy(decoded_.source.edition.bytes.data(), bytes.data() + 76, 16);
  std::memcpy(decoded_.source.revision.bytes.data(), bytes.data() + 92, 32);
  decoded_.scope.server = take(18);
  decoded_.scope.account = take(20);
  decoded_.payload = take(22);
  decoded_.value.raw = take(24);
  decoded_.value.label = take(26);
  decoded_.document = take(124);
  decoded_.deliver = flags & 1;
  decoded_.value.present = flags & 2;
  decoded_.value.percentagePresent = flags & 4;
  decoded_.value.reset = flags & 8;
  decoded_.value.redated = flags & 16;
  decoded_.wasOverride = flags & 32;
  decoded_.value.browser = flags & 64;
  decoded_.value.kind = get(bytes.data() + 14, 2);
  decoded_.value.version = get(bytes.data() + 16, 2);
  return at == bytes.size() && valid(decoded_);
}
bool ProgressCapture::validateFile(FsFile& file, void* context) {
  auto& capture = *static_cast<ProgressCapture*>(context);
  const auto size = file.fileSize();
  if (!capture.scratch_ || capture.capacity_ < MaxBytes || size < Header || size > MaxBytes ||
      file.read(capture.scratch_, size) != static_cast<int>(size))
    return false;
  char extra;
  return file.read(&extra, 1) == 0 && capture.decode({capture.scratch_, size});
}
bool ProgressCapture::load(storage::ScopeView scope, const storage::SourceIdentity& source, uint64_t entry,
                           uint64_t nativeOpen, std::string_view payload, std::string_view document,
                           FrozenProgress& output) {
  if (!text(scope.server, 128, true) || !text(scope.account, 128, true) || overlaps(payload) || overlaps(document))
    return false;
  const storage::SourceIdentity expectedSource = source;
  char server[129]{};
  std::memcpy(server, scope.server.data(), scope.server.size());
  std::memmove(account_.data(), scope.account.data(), scope.account.size());
  std::memset(account_.data() + scope.account.size(), 0, account_.size() - scope.account.size());
  std::memcpy(server_.data(), server, sizeof(server));
  if (!storage::FilePublish::validateCurrent(paths_, validateFile, this) || decoded_.scope.server != server_.data() ||
      decoded_.scope.account != account_.data() || decoded_.source != expectedSource || decoded_.entry != entry ||
      decoded_.nativeOpen != nativeOpen || decoded_.payload != payload || decoded_.document != document) {
    LOG_ERR("BSP", "Frozen progress unavailable or binding changed");
    return false;
  }
  output = decoded_;
  return true;
}
bool ProgressCapture::freeze(const FrozenProgress& value) {
  if (!scratch_ || capacity_ < MaxBytes || overlaps(value.scope.server) || overlaps(value.scope.account) ||
      overlaps(value.payload) || overlaps(value.document) || overlaps(value.value.raw) || overlaps(value.value.label) ||
      !valid(value)) {
    LOG_ERR("BSP", "Invalid frozen progress or overlapping workspace");
    return false;
  }
  storage::FilePublish writer(paths_, validateFile, this);
  if (Storage.exists(paths_.current) || Storage.exists(paths_.backup)) {
    if (!storage::FilePublish::validateCurrent(paths_, validateFile, this) || !same(value, decoded_)) {
      LOG_ERR("BSP", "Existing progress capture cannot be replaced");
      return false;
    }
    return writer.confirm();
  }
  // Bounded codec header, below 256 bytes. Payloads stream from caller-owned
  // views; the 8 KiB decode workspace belongs to the worker, never this frame.
  char header[Header]{};
  std::memcpy(header, "BPG1", 4);
  const auto size = Header + value.scope.server.size() + value.scope.account.size() + value.payload.size() +
                    value.value.raw.size() + value.value.label.size() + value.document.size();
  put(header + 4, size, 4);
  put(header + 12,
      value.deliver | value.value.present << 1 | value.value.percentagePresent << 2 | value.value.reset << 3 |
          value.value.redated << 4 | value.wasOverride << 5 | value.value.browser << 6,
      2);
  put(header + 14, value.value.kind, 2);
  put(header + 16, value.value.version, 2);
  put(header + 18, value.scope.server.size(), 2);
  put(header + 20, value.scope.account.size(), 2);
  put(header + 22, value.payload.size(), 2);
  put(header + 24, value.value.raw.size(), 2);
  put(header + 26, value.value.label.size(), 2);
  put(header + 124, value.document.size(), 2);
  put(header + 28, value.entry, 8);
  put(header + 36, value.nativeOpen, 8);
  put(header + 44, value.value.at, 8);
  uint64_t fraction;
  std::memcpy(&fraction, &value.value.percentage, 8);
  put(header + 52, fraction, 8);
  std::memcpy(header + 60, value.source.book.bytes.data(), 16);
  std::memcpy(header + 76, value.source.edition.bytes.data(), 16);
  std::memcpy(header + 92, value.source.revision.bytes.data(), 32);
  auto sum = checksum({header + 12, Header - 12});
  for (const auto part :
       {value.scope.server, value.scope.account, value.payload, value.value.raw, value.value.label, value.document})
    sum = checksum(part, sum);
  put(header + 8, sum, 4);
  if (size > MaxBytes || !writer.begin(size) || !writer.write(header, Header)) return false;
  for (const auto part :
       {value.scope.server, value.scope.account, value.payload, value.value.raw, value.value.label, value.document})
    if (!part.empty() && !writer.write(part.data(), part.size())) return false;
  return writer.finishNew();
}
}  // namespace booksync::server
