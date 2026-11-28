// SPDX-License-Identifier: GPL-3.0-or-later
#include "AnnotationSpool.h"

#include <Logging.h>

#include <algorithm>
#include <cstring>

#include "booksync/reader/Capture.h"

namespace booksync::server {
namespace {
uint64_t get(const char* bytes, unsigned n) {
  uint64_t result = 0;
  for (unsigned i = 0; i < n; ++i) result |= uint64_t(uint8_t(bytes[i])) << (8 * i);
  return result;
}
void put(char* bytes, uint64_t value, unsigned n) {
  for (unsigned i = 0; i < n; ++i) bytes[i] = char(value >> (8 * i));
}
uint32_t checksum(std::string_view bytes, uint32_t value = 2166136261U) {
  for (const auto c : bytes) value = (value ^ uint8_t(c)) * 16777619U;
  return value;
}
bool text(std::string_view bytes, size_t max) {
  return !bytes.empty() && bytes.size() <= max && bytes.find('\0') == bytes.npos;
}
bool keyValid(const AnnotationKey& key) {
  return key.key.size() == 32 &&
         std::all_of(key.key.begin(), key.key.end(),
                     [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) &&
         text(key.datetime, 19) && key.datetime.size() == 19;
}
struct Measure {
  size_t size = 0;
  uint32_t sum = 2166136261U;
  static bool write(void* context, std::string_view bytes) {
    auto& value = *static_cast<Measure*>(context);
    if (bytes.size() > UINT32_MAX - value.size) return false;
    value.size += bytes.size();
    value.sum = checksum(bytes, value.sum);
    return true;
  }
};
bool writeFile(void* context, std::string_view bytes) {
  return static_cast<storage::FilePublish*>(context)->write(bytes.data(), bytes.size());
}
}  // namespace
bool AnnotationSpool::validBinding() const {
  reader::OpenCapture opened;
  if (!scratch_ || capacity_ < AnnotationRecordCodec::MaxBytes || !binding_.entry || !binding_.nativeOpen ||
      !storage::valid(binding_.source) || !text(binding_.scope.server, 128) || !text(binding_.scope.account, 128) ||
      !text(binding_.document, 128) || binding_.payload.size() > 2512 ||
      !reader::Capture::decodeOpen(binding_.payload, opened) || !opened.bound ||
      opened.binding.scope != binding_.scope || opened.binding.source != binding_.source)
    return false;
  // The codec/validator reuse scratch and cannot preserve a binding borrowed from it.
  for (const auto part : {binding_.scope.server, binding_.scope.account, binding_.payload, binding_.document}) {
    const auto p = reinterpret_cast<uintptr_t>(part.data()), s = reinterpret_cast<uintptr_t>(scratch_);
    if ((p >= s && p - s < capacity_) || (p < s && s - p < part.size())) return false;
  }
  return uint8_t(kind_) <= uint8_t(AnnotationKind::Bookmark);
}
bool AnnotationSpool::validState(const AnnotationSpoolState& value) const {
  return !(value.flags & ~63U) && value.rows <= (pending_ ? LifecycleAnnotations::MaxRemoteChanges : 1024) &&
         value.keys <= LifecycleAnnotations::MaxKeys && (!pending_ || !value.keys) &&
         value.nextCursor >= value.cursor && value.nextCursor - value.cursor <= LifecycleAnnotations::MaxChanges;
}
bool AnnotationSpool::emit(AnnotationRecordReader& rows, AnnotationKeyReader* keys, AnnotationRecordSink sink) {
  for (size_t i = 0; i < state_.rows; ++i) {
    if (!sourceAdmitted()) return false;
    const auto* record = rows.at(i);
    if (!record || !AnnotationRecordCodec::write(*record, sink, rowHeader_.data(), rowHeader_.size())) return false;
  }
  for (size_t i = 0; i < state_.keys; ++i) {
    if (!sourceAdmitted()) return false;
    const auto* value = keys ? keys->at(i) : nullptr;
    if (!value || !keyValid(*value) || !sink.write(sink.context, value->key) ||
        !sink.write(sink.context, value->datetime))
      return false;
  }
  return true;
}
bool AnnotationSpool::create(const AnnotationSpoolState& state, AnnotationRecordReader& rows,
                             AnnotationKeyReader* keys) {
  loaded_ = false;
  if (!validBinding() || !validState(state) || !sourceAdmitted() || Storage.exists(paths_.current) ||
      Storage.exists(paths_.backup))
    return false;
  state_ = state;
  header_.fill(0);
  auto* h = header_.data();
  std::memcpy(h, pending_ ? "BAP1" : "BAF1", 4);
  put(h + 12, state.flags | uint16_t(state.nextDone) << 6, 2);
  h[14] = char(kind_);
  put(h + 16, binding_.entry, 8);
  put(h + 24, binding_.nativeOpen, 8);
  put(h + 32, state.round, 8);
  put(h + 40, state.cursor, 4);
  put(h + 44, state.nextCursor, 4);
  put(h + 48, state.rows, 4);
  put(h + 52, state.keys, 4);
  std::memcpy(h + 56, binding_.source.book.bytes.data(), 16);
  std::memcpy(h + 72, binding_.source.edition.bytes.data(), 16);
  std::memcpy(h + 88, binding_.source.revision.bytes.data(), 32);
  put(h + 128, state.nativeGeneration, 8);
  size_t offset = 120;
  Measure measured;
  for (const auto part : {binding_.scope.server, binding_.scope.account, binding_.payload, binding_.document}) {
    put(h + offset, part.size(), 2);
    offset += 2;
  }
  // Length fields belong to the checksummed header, so measure only after filling them.
  Measure::write(&measured, {h + 12, header_.size() - 12});
  for (const auto part : {binding_.scope.server, binding_.scope.account, binding_.payload, binding_.document})
    if (!Measure::write(&measured, part)) return false;
  if (!emit(rows, keys, {&measured, Measure::write})) return false;
  put(h + 4, measured.size + 12, 4);
  put(h + 8, measured.sum, 4);
  storage::FilePublish writer(paths_, validateFile, this);
  if (!writer.begin(measured.size + 12) || !writer.write(h, header_.size())) return false;
  for (const auto part : {binding_.scope.server, binding_.scope.account, binding_.payload, binding_.document})
    if (!writer.write(part.data(), part.size())) return false;
  if (!emit(rows, keys, {&writer, writeFile}) || !sourceAdmitted() || !writer.finishNew()) {
    LOG_ERR("BSA", "Annotation spool publication incomplete");
    return false;
  }
  return true;
}
bool AnnotationSpool::readRecord(FsFile& file, AnnotationRecord& result) {
  keyCacheCount_ = 0;
  if (!sourceAdmitted() || file.read(scratch_, AnnotationRecordCodec::Header) != int(AnnotationRecordCodec::Header))
    return false;
  const auto size = get(scratch_ + 4, 4);
  return size >= AnnotationRecordCodec::Header && size <= AnnotationRecordCodec::MaxBytes &&
         file.read(scratch_ + AnnotationRecordCodec::Header, size - AnnotationRecordCodec::Header) ==
             int(size - AnnotationRecordCodec::Header) &&
         AnnotationRecordCodec::decode({scratch_, size}, result);
}
bool AnnotationSpool::validate(FsFile& file) {
  loaded_ = false;
  if (!validBinding() || !sourceAdmitted() || file.read(header_.data(), header_.size()) != int(header_.size()))
    return false;
  const auto* h = header_.data();
  if (std::string_view(h, 4) != (pending_ ? "BAP1" : "BAF1") || get(h + 4, 4) != file.fileSize() ||
      (get(h + 12, 2) & ~127U) || uint8_t(h[14]) != uint8_t(kind_) || h[15] || get(h + 16, 8) != binding_.entry ||
      get(h + 24, 8) != binding_.nativeOpen || std::memcmp(h + 56, binding_.source.book.bytes.data(), 16) ||
      std::memcmp(h + 72, binding_.source.edition.bytes.data(), 16) ||
      std::memcmp(h + 88, binding_.source.revision.bytes.data(), 32))
    return false;
  state_ = {};
  state_.flags = uint8_t(h[12]) & 63;
  state_.nextDone = h[12] & 64;
  state_.round = get(h + 32, 8);
  state_.cursor = get(h + 40, 4);
  state_.nextCursor = get(h + 44, 4);
  state_.rows = get(h + 48, 4);
  state_.keys = get(h + 52, 4);
  state_.nativeGeneration = get(h + 128, 8);
  if (!validState(state_)) return false;
  auto sum = checksum({h + 12, header_.size() - 12});
  size_t offset = 120;
  for (const auto part : {binding_.scope.server, binding_.scope.account, binding_.payload, binding_.document}) {
    if (get(h + offset, 2) != part.size() || file.read(scratch_, part.size()) != int(part.size()) ||
        std::string_view(scratch_, part.size()) != part)
      return false;
    sum = checksum({scratch_, part.size()}, sum);
    offset += 2;
  }
  rowsAt_ = file.position();
  for (size_t i = 0; i < state_.rows; ++i) {
    if (!readRecord(file, decoded_)) return false;
    sum = checksum({scratch_, size_t(get(scratch_ + 4, 4))}, sum);
  }
  keysAt_ = file.position();
  for (size_t i = 0; i < state_.keys; ++i) {
    if (!sourceAdmitted() || file.read(scratch_, 51) != 51 || !keyValid({{scratch_, 32}, {scratch_ + 32, 19}}))
      return false;
    sum = checksum({scratch_, 51}, sum);
  }
  char extra;
  if (file.read(&extra, 1) != 0 || sum != get(h + 8, 4)) return false;
  nextIndex_ = 0;
  nextAt_ = rowsAt_;
  keyCacheCount_ = 0;
  loaded_ = true;
  return true;
}
bool AnnotationSpool::validateFile(FsFile& file, void* context) {
  return static_cast<AnnotationSpool*>(context)->validate(file);
}
bool AnnotationSpool::load(AnnotationSpoolState& output) {
  loaded_ = false;
  // Immutable authority must be current. A retained backup or partial staging is
  // evidence for inspection, never permission to replay a different reply.
  storage::FilePublish publication(paths_, validateFile, this);
  if (!publication.confirm() || !sourceAdmitted()) {
    loaded_ = false;
    return false;
  }
  output = state_;
  return true;
}
const AnnotationRecord* AnnotationSpool::at(size_t index) {
  if (!loaded_ || index >= state_.rows || !sourceAdmitted()) return nullptr;
  auto file = Storage.open(paths_.current, O_RDONLY);
  if (!file) return nullptr;
  if (index < nextIndex_) {
    nextIndex_ = 0;
    nextAt_ = rowsAt_;
  }
  bool ok = file.seekSet(nextAt_);
  while (ok && nextIndex_ <= index) {
    ok = readRecord(file, decoded_);
    if (ok) {
      ++nextIndex_;
      nextAt_ = file.position();
    }
  }
  const bool closed = file.close();
  if (!ok || !closed) {
    loaded_ = false;
    return nullptr;
  }
  return &decoded_;
}
bool AnnotationSpool::key(size_t index, AnnotationKey& output) {
  if (!loaded_ || index >= state_.keys || !sourceAdmitted()) return false;
  if (index < keyCacheIndex_ || index - keyCacheIndex_ >= keyCacheCount_) {
    auto file = Storage.open(paths_.current, O_RDONLY);
    if (!file) return false;
    // Reuse record storage after row serialization; no extra key array. At most
    // two reads serve a complete 1024-key snapshot instead of 1024 SD opens.
    const size_t count = std::min<size_t>(512, state_.keys - index);
    const bool ok =
        file.seekSet(keysAt_ + index * 51) && file.read(scratch_, count * 51) == static_cast<int>(count * 51);
    const bool closed = file.close();
    keyCacheCount_ = 0;
    if (!ok || !closed) return false;
    keyCacheIndex_ = index;
    keyCacheCount_ = count;
  }
  const auto* bytes = scratch_ + (index - keyCacheIndex_) * 51;
  output = {{bytes, 32}, {bytes + 32, 19}};
  return keyValid(output);
}
}  // namespace booksync::server
