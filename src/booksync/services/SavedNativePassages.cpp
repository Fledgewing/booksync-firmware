// SPDX-License-Identifier: GPL-3.0-or-later
#include "SavedNativePassages.h"

#include <HalStorage.h>
#include <Logging.h>
#include <uzlib.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
namespace booksync {
namespace {
bool textValid(std::string_view text) {
  for (size_t at = 0; at < text.size();) {
    const auto byte = static_cast<uint8_t>(text[at++]);
    if (byte < 32 && byte != '\n' && byte != '\r' && byte != '\t') return false;
    if (byte < 128) continue;
    uint32_t cp = 0, minimum = 0;
    unsigned continuation = 0;
    if (byte >= 0xc2 && byte <= 0xdf) {
      cp = byte & 31;
      minimum = 128;
      continuation = 1;
    } else if (byte >= 0xe0 && byte <= 0xef) {
      cp = byte & 15;
      minimum = 2048;
      continuation = 2;
    } else if (byte >= 0xf0 && byte <= 0xf4) {
      cp = byte & 7;
      minimum = 65536;
      continuation = 3;
    } else
      return false;
    if (continuation > text.size() - at) return false;
    while (continuation--) {
      const auto next = static_cast<uint8_t>(text[at++]);
      if ((next & 0xc0) != 0x80) return false;
      cp = (cp << 6) | (next & 63);
    }
    if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
  }
  return true;
}
template <class T>
bool little(FsFile& file, T& result) {
  uint8_t bytes[sizeof(T)];
  if (file.read(bytes, sizeof(bytes)) != int(sizeof(bytes))) return false;
  result = 0;
  for (unsigned i = 0; i < sizeof(T); ++i) result |= T(bytes[i]) << (i * 8);
  return true;
}
}  // namespace
NativePersonalState SavedNativePassages::state(void* context) {
  auto& self = *static_cast<SavedNativePassages*>(context);
  return {self.index_.provenance(), self.index_.count(), self.index_.ready()};
}
NativePassageStep SavedNativePassages::passage(void* context, uint16_t book, uint16_t clip, PersonalRecord& out) {
  return static_cast<SavedNativePassages*>(context)->load(book, clip, out);
}
bool SavedNativePassages::skip(FsFile& file, uint32_t count) {
  const auto position = file.position();
  return position <= file.fileSize() && count <= file.fileSize() - position && file.seekSet(position + count);
}
bool SavedNativePassages::field(FsFile& file, bool comparePath) {
  uint32_t bytes = 0;
  if (!little(file, bytes) || bytes > 4096) return false;
  if (!comparePath) return skip(file, bytes);
  if (bytes != std::strlen(book_.path.data())) return false;
  uint32_t at = 0;
  while (at < bytes) {
    const auto n = std::min<uint32_t>(bytes - at, buffer_.size());
    if (file.read(buffer_.data(), n) != int(n) || std::memcmp(buffer_.data(), book_.path.data() + at, n)) return false;
    at += n;
  }
  return true;
}
bool SavedNativePassages::header(FsFile& file, uint8_t& version, uint16_t& count) {
  if (!little(file, version) || version < 1 || version > 7 || !little(file, count) || count > 256 ||
      !field(file, false) || !field(file, false) || !field(file, true))
    return false;
  if (version < 5) return true;
  uint16_t deleted = 0;
  if (!little(file, deleted) || deleted > 256) return false;
  for (unsigned i = 0; i < deleted; ++i) {
    uint32_t length = 0;
    if (!little(file, length) || !length || length > (version >= 6 ? 80u : 64u) || (version >= 6 && length <= 16) ||
        !skip(file, length))
      return false;
  }
  return true;
}
NativePassageStep SavedNativePassages::record(FsFile& file, uint8_t version, bool selected, PersonalRecord& out) {
  for (auto& coordinate : coordinates_)
    if (!little(file, coordinate)) return NativePassageStep::Error;
  if (!little(file, timestamp_) || (version >= 3 && !skip(file, 4)) || (version >= 4 && !skip(file, 2)))
    return NativePassageStep::Error;
  if (version >= 7) {
    uint32_t start = 0, end = 0;
    if (!little(file, start) || !little(file, end) ||
        !((start < end && end != UINT32_MAX) || (start == UINT32_MAX && (end == UINT32_MAX || end == 0))))
      return NativePassageStep::Error;
  }
  uint32_t length = 0;
  uint8_t flags = 0;
  key_.fill(0);
  if (version >= 5) {
    if (!little(file, length) || length > 80 || file.read(key_.data(), length) != int(length) || !little(file, flags) ||
        flags > (version >= 6 ? 3 : 1) || ((flags & 2) ? length <= 16 : length > 64))
      return NativePassageStep::Error;
  }
  if (file.read(buffer_.data(), 48) != 48) return NativePassageStep::Error;
  buffer_[47] = 0;
  uint32_t text = 0;
  if (version == 1) {
    if (!little(file, text)) return NativePassageStep::Error;
  } else {
    uint16_t shortText = 0;
    if (!little(file, shortText)) return NativePassageStep::Error;
    text = shortText;
  }
  if (text > 4096) return NativePassageStep::Error;
  // A scoped remote/native-sync record is not evidence of account-independent
  // ownership. Leave it to a verified scoped account provider.
  if (!selected || (flags & 2) || !text) return skip(file, text) ? NativePassageStep::Skip : NativePassageStep::Error;
  if (text >= out.passage.size()) return NativePassageStep::Oversized;
  out.identity.fill(0);
  out.passage.fill(0);
  out.source.fill(0);
  if (file.read(out.passage.data(), text) != int(text) || std::memchr(out.passage.data(), 0, text) ||
      !textValid({out.passage.data(), text}))
    return NativePassageStep::Error;
  // The full native path and retained key/coordinates are identity evidence.
  // CRC below selects a file only; its header was compared byte for byte.
  int used = std::snprintf(out.identity.data(), out.identity.size(), "native:%zu:%s:", std::strlen(book_.path.data()),
                           book_.path.data());
  if (used < 0 || size_t(used) >= out.identity.size()) return NativePassageStep::Oversized;
  if (length) {
    // Native keys are textual; embedded NUL cannot safely identify the record.
    if (std::memchr(key_.data(), 0, length)) return NativePassageStep::Error;
    used += std::snprintf(out.identity.data() + used, out.identity.size() - used, "key:%s", key_.data());
  } else {
    used += std::snprintf(out.identity.data() + used, out.identity.size() - used, "range:%u:%u:%u:%u:%u:%u:%u:%u:%lu",
                          coordinates_[0], coordinates_[1], coordinates_[2], coordinates_[3], coordinates_[4],
                          coordinates_[5], coordinates_[6], coordinates_[7], static_cast<unsigned long>(timestamp_));
  }
  if (used < 0 || size_t(used) >= out.identity.size()) return NativePassageStep::Oversized;
  if (textValid(buffer_.data())) std::snprintf(out.source.data(), out.source.size(), "%s", buffer_.data());
  return NativePassageStep::Record;
}
NativePassageStep SavedNativePassages::load(uint16_t book, uint16_t clipping, PersonalRecord& out) {
  const BookKey key{index_.provenance(), book};
  if (!index_.read(key, book_)) return NativePassageStep::Error;
  const auto n = std::strlen(book_.path.data());
  if (!n || n > 1024) return NativePassageStep::Error;
  const uint32_t crc = uzlib_crc32(book_.path.data(), static_cast<unsigned>(n), 0);
  std::snprintf(filePath_.data(), filePath_.size(), "/.crosspoint/clippings/epub_%lu.bin",
                static_cast<unsigned long>(crc));
  if (!Storage.exists(filePath_.data())) {
    const auto length = std::strlen(filePath_.data());
    std::memcpy(filePath_.data() + length, ".bak", 5);
    if (!Storage.exists(filePath_.data())) return NativePassageStep::Missing;
  }
  FsFile file = Storage.open(filePath_.data(), O_RDONLY);
  if (!file) return NativePassageStep::Error;
  uint8_t version = 0;
  uint16_t count = 0;
  NativePassageStep result = NativePassageStep::Error;
  if (file.fileSize() > 2 * 1024 * 1024)
    result = NativePassageStep::Oversized;
  else if (header(file, version, count)) {
    if (clipping >= count)
      result = NativePassageStep::End;
    else {
      for (unsigned i = 0; i <= clipping; ++i) {
        result = record(file, version, i == clipping, out);
        if (result == NativePassageStep::Error || result == NativePassageStep::Oversized) break;
      }
    }
  }
  if (!file.close()) result = NativePassageStep::Error;
  if (result == NativePassageStep::Record && !index_.summary(key, out.book)) result = NativePassageStep::Error;
  if (result == NativePassageStep::Error) LOG_ERR("BSP", "Saved native passage rejected; original retained");
  return result;
}
}  // namespace booksync
