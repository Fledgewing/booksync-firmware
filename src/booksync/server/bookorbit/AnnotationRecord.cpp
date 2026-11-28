// SPDX-License-Identifier: GPL-3.0-or-later
#include "AnnotationRecord.h"

#include <Logging.h>

#include <cstring>
#include <limits>

namespace booksync::server {
namespace {
constexpr std::array<uint16_t, 12> Caps{19, 19, 4000, 4000, 32, 32, 32, 10000, 5000, 500, 500, 32};
std::array<std::string_view, 12> parts(const AnnotationRow& row) {
  return {row.datetime, row.updated, row.start, row.end,     row.format, row.drawer,
          row.color,    row.text,    row.note,  row.chapter, row.title,  row.key};
}
void put(char* p, uint64_t value, unsigned bytes) {
  for (unsigned i = 0; i < bytes; ++i) p[i] = static_cast<char>(value >> (8 * i));
}
uint64_t get(const char* p, unsigned bytes) {
  uint64_t value = 0;
  for (unsigned i = 0; i < bytes; ++i) value |= uint64_t(uint8_t(p[i])) << (8 * i);
  return value;
}
uint32_t checksum(std::string_view bytes, uint32_t value = 2166136261u) {
  for (uint8_t byte : bytes) value = (value ^ byte) * 16777619u;
  return value;
}
bool overlap(const void* first, size_t count, const void* second, size_t other) {
  const auto a = reinterpret_cast<uintptr_t>(first), b = reinterpret_cast<uintptr_t>(second);
  return a + count < a || b + other < b || (a < b + other && b < a + count);
}
bool metadataAlias(const AnnotationRow& row, const std::string_view& output) {
  return &output == &row.datetime || &output == &row.updated || &output == &row.start || &output == &row.end ||
         &output == &row.format || &output == &row.drawer || &output == &row.color || &output == &row.text ||
         &output == &row.note || &output == &row.chapter || &output == &row.title || &output == &row.key;
}
bool invalid() {
  LOG_ERR("BAN", "Invalid annotation record framing");
  return false;
}
bool scalars(const AnnotationRecord& value) {
  return value.change.op <= AnnotationOp::Delete && value.origin <= AnnotationOrigin::Remote &&
         (!value.verified || value.applied) &&
         value.change.row.serverId <= uint64_t(std::numeric_limits<int64_t>::max()) &&
         value.change.row.version <= uint64_t(std::numeric_limits<int64_t>::max());
}
}  // namespace
namespace {
void header(const AnnotationRecord& value, char* bytes, size_t length) {
  std::memset(bytes, 0, AnnotationRecordCodec::Header);
  std::memcpy(bytes, "BAR1", 4);
  put(bytes + 4, length, 4);
  put(bytes + 12, value.change.row.serverId, 8);
  put(bytes + 20, value.change.row.version, 8);
  put(bytes + 28, value.change.row.page, 4);
  put(bytes + 32, value.change.row.fields, 2);
  bytes[34] = static_cast<char>(value.change.op);
  bytes[35] = value.change.row.pendingVerification;
  std::memcpy(bytes + 36, value.nativeId.data(), value.nativeId.size());
  bytes[76] = static_cast<char>(value.origin);
  bytes[77] = value.applied | (value.verified << 1);
  size_t index = 0;
  for (const auto part : parts(value.change.row)) put(bytes + 52 + 2 * index++, part.size(), 2);
  auto sum = checksum({bytes + 12, AnnotationRecordCodec::Header - 12});
  for (const auto part : parts(value.change.row)) sum = checksum(part, sum);
  put(bytes + 8, sum, 4);
}
bool writeFailed() {
  LOG_ERR("BAN", "Annotation record stream write failed");
  return false;
}
}  // namespace
size_t AnnotationRecordCodec::size(const AnnotationRecord& value) {
  if (!scalars(value)) {
    invalid();
    return 0;
  }
  size_t length = Header, index = 0;
  for (const auto part : parts(value.change.row)) {
    if (part.size() > Caps[index++]) {
      invalid();
      return 0;
    }
    length += part.size();
  }
  return length <= MaxBytes ? length : 0;
}
bool AnnotationRecordCodec::encode(const AnnotationRecord& value, char* bytes, size_t capacity,
                                   std::string_view& output) {
  const auto length = size(value);
  if (!bytes || !length || length > capacity || metadataAlias(value.change.row, output) ||
      overlap(bytes, length, &value, sizeof(value)))
    return invalid();
  for (const auto part : parts(value.change.row))
    if (!part.empty() && overlap(bytes, length, part.data(), part.size())) return invalid();
  header(value, bytes, length);
  size_t offset = Header;
  for (const auto part : parts(value.change.row)) {
    if (!part.empty()) std::memcpy(bytes + offset, part.data(), part.size());
    offset += part.size();
  }
  output = {bytes, length};
  return true;
}
bool AnnotationRecordCodec::write(const AnnotationRecord& value, AnnotationRecordSink sink, char* bytes,
                                  size_t capacity) {
  const auto length = size(value);
  if (!bytes || !sink.write || !length || capacity < Header || overlap(bytes, Header, &value, sizeof(value)))
    return invalid();
  for (const auto part : parts(value.change.row))
    if (!part.empty() && overlap(bytes, Header, part.data(), part.size())) return invalid();
  header(value, bytes, length);
  if (!sink.write(sink.context, {bytes, Header})) return writeFailed();
  for (const auto part : parts(value.change.row))
    if (!part.empty() && !sink.write(sink.context, part)) return writeFailed();
  return true;
}
bool AnnotationRecordCodec::decode(std::string_view bytes, AnnotationRecord& output) {
  if (bytes.size() < Header || bytes.size() > MaxBytes || bytes.substr(0, 4) != "BAR1" ||
      overlap(bytes.data(), bytes.size(), &output, sizeof(output)) || get(bytes.data() + 4, 4) != bytes.size() ||
      get(bytes.data() + 8, 4) != checksum(bytes.substr(12)) || bytes[78] || bytes[79] || (uint8_t(bytes[35]) & ~1u) ||
      (uint8_t(bytes[77]) & ~3u))
    return invalid();
  size_t size = Header;
  for (size_t i = 0; i < Caps.size(); ++i) {
    const auto length = get(bytes.data() + 52 + 2 * i, 2);
    if (length > Caps[i] || length > bytes.size() - size) return invalid();
    size += length;
  }
  if (size != bytes.size()) return invalid();
  AnnotationRecord value;
  auto& row = value.change.row;
  row.serverId = get(bytes.data() + 12, 8);
  row.version = get(bytes.data() + 20, 8);
  row.page = static_cast<uint32_t>(get(bytes.data() + 28, 4));
  row.fields = static_cast<uint16_t>(get(bytes.data() + 32, 2));
  value.change.op = static_cast<AnnotationOp>(uint8_t(bytes[34]));
  row.pendingVerification = bytes[35];
  std::memcpy(value.nativeId.data(), bytes.data() + 36, value.nativeId.size());
  value.origin = static_cast<AnnotationOrigin>(uint8_t(bytes[76]));
  value.applied = bytes[77] & 1;
  value.verified = bytes[77] & 2;
  if (!scalars(value)) return invalid();
  std::string_view* fields[]{&row.datetime, &row.updated, &row.start, &row.end,     &row.format, &row.drawer,
                             &row.color,    &row.text,    &row.note,  &row.chapter, &row.title,  &row.key};
  size_t offset = Header;
  for (size_t i = 0; i < Caps.size(); ++i) {
    const auto length = get(bytes.data() + 52 + 2 * i, 2);
    *fields[i] = bytes.substr(offset, length);
    offset += length;
  }
  output = value;
  return true;
}
}  // namespace booksync::server
