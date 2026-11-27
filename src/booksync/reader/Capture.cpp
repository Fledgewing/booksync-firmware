// SPDX-License-Identifier: GPL-3.0-or-later
#include "Capture.h"

#include <Logging.h>

#include <algorithm>
#include <cstring>

namespace booksync::reader {
namespace {
void put(char* out, uint32_t n) {
  for (unsigned i = 0; i < 4; ++i) out[i] = static_cast<char>(n >> (i * 8));
}
uint32_t get(const char* in) {
  uint32_t n = 0;
  for (unsigned i = 0; i < 4; ++i) n |= uint32_t(static_cast<uint8_t>(in[i])) << (i * 8);
  return n;
}
void putWide(char* p, uint64_t n, size_t bytes) {
  for (size_t i = 0; i < bytes; ++i) p[i] = static_cast<char>(n >> (8 * i));
}
constexpr size_t Bytes = 116;
}  // namespace
storage::JournalResult Capture::open(std::string_view path, uint16_t nativeFormat) {
  if (id_ || path.empty() || path.size() > 1024 || path.find('\0') != path.npos || !nativeFormat)
    return storage::JournalResult::Invalid;
  // Maximum 1032 bytes, too large for the C3 task stack. Shared scratch is safe
  // because the sole local capture writer runs on the main task under RenderLock.
  static char payload[1032];
  memcpy(payload, "BSO1", 4);
  payload[4] = static_cast<char>(nativeFormat);
  payload[5] = static_cast<char>(nativeFormat >> 8);
  payload[6] = static_cast<char>(path.size());
  payload[7] = static_cast<char>(path.size() >> 8);
  memcpy(payload + 8, path.data(), path.size());
  return journal_.enqueue({0, "native", "local-unbound", "native-source", {payload, path.size() + 8}, {}, 0}, id_);
}
storage::JournalResult Capture::update(Sample sample, bool closed) {
  if (!id_ || (closed_ && !closed)) return storage::JournalResult::Invalid;
  sample.openingTotalSeconds = sampled_ ? previous_.openingTotalSeconds : sample.totalSeconds;
  sample.openingCounter = sampled_ ? previous_.openingCounter : sample.counter;
  if (sampled_ && previous_ == sample && closed_ == closed) return storage::JournalResult::Ok;
  char bytes[Bytes]{};
  memcpy(bytes, "BSC1", 4);
  bytes[4] = closed;
  bytes[5] = sample.hasSourceOffset;
  bytes[6] = sample.hasStats;
  put(bytes + 8, sample.spine);
  put(bytes + 12, sample.page);
  put(bytes + 16, sample.pages);
  put(bytes + 20, sample.sourceOffset);
  put(bytes + 24, sample.totalSeconds);
  put(bytes + 28, sample.sessionSeconds);
  put(bytes + 32, sample.forwardTurns);
  memcpy(bytes + 36, sample.counter.data(), sample.counter.size());
  memcpy(bytes + 52, sample.rawProgress.data(), sample.rawProgress.size());
  bytes[92] = sample.primaryBytes;
  bytes[93] = sample.backupBytes;
  bytes[94] = static_cast<char>(sample.nativeSessions);
  bytes[95] = static_cast<char>(sample.nativeSessions >> 8);
  put(bytes + 96, sampled_ ? previous_.openingTotalSeconds : sample.totalSeconds);
  const auto& openingCounter = sampled_ ? previous_.openingCounter : sample.counter;
  memcpy(bytes + 100, openingCounter.data(), openingCounter.size());
  const auto result = journal_.checkpoint(id_, {bytes, sizeof(bytes)});
  if (result == storage::JournalResult::Ok) {
    previous_ = sample;
    previous_.openingTotalSeconds = get(bytes + 96);
    memcpy(previous_.openingCounter.data(), bytes + 100, previous_.openingCounter.size());
    sampled_ = true;
    closed_ = closed;
  } else
    LOG_ERR("BSC", "Native checkpoint retained for retry: %u", unsigned(result));
  return result;
}
bool Capture::decode(std::string_view checkpoint, Sample& sample, bool& closed) {
  if (checkpoint.size() != Bytes || checkpoint.substr(0, 4) != "BSC1" || static_cast<uint8_t>(checkpoint[4]) > 1 ||
      static_cast<uint8_t>(checkpoint[5]) > 1 || static_cast<uint8_t>(checkpoint[6]) > 1 || checkpoint[7])
    return false;
  if (static_cast<uint8_t>(checkpoint[92]) > 20 || static_cast<uint8_t>(checkpoint[93]) > 20) return false;
  Sample value;
  value.spine = get(checkpoint.data() + 8);
  value.page = get(checkpoint.data() + 12);
  value.pages = get(checkpoint.data() + 16);
  value.sourceOffset = get(checkpoint.data() + 20);
  value.totalSeconds = get(checkpoint.data() + 24);
  value.sessionSeconds = get(checkpoint.data() + 28);
  value.forwardTurns = get(checkpoint.data() + 32);
  value.hasSourceOffset = checkpoint[5];
  value.hasStats = checkpoint[6];
  memcpy(value.counter.data(), checkpoint.data() + 36, value.counter.size());
  memcpy(value.rawProgress.data(), checkpoint.data() + 52, value.rawProgress.size());
  value.primaryBytes = static_cast<uint8_t>(checkpoint[92]);
  value.backupBytes = static_cast<uint8_t>(checkpoint[93]);
  value.nativeSessions = static_cast<uint8_t>(checkpoint[94]) | (uint16_t(static_cast<uint8_t>(checkpoint[95])) << 8);
  value.openingTotalSeconds = get(checkpoint.data() + 96);
  memcpy(value.openingCounter.data(), checkpoint.data() + 100, value.openingCounter.size());
  sample = value;
  closed = checkpoint[4];
  return true;
}
storage::JournalResult Capture::session(const SessionObservation& observation) {
  using storage::JournalResult;
  if (!observation.id[0] || observation.seconds < 10) return JournalResult::Ok;
  if (sessionId_ && session_ == observation) return JournalResult::Ok;
  char bytes[90]{};
  memcpy(bytes, "BSS1", 4);
  memcpy(bytes + 4, observation.id.data(), 32);
  memcpy(bytes + 36, observation.document.data(), 32);
  putWide(bytes + 68, observation.startedAt, 8);
  putWide(bytes + 76, observation.endedAt, 8);
  put(bytes + 84, observation.seconds);
  putWide(bytes + 88, static_cast<uint16_t>(observation.utcOffsetMinutes), 2);
  const bool same = sessionId_ && session_.id == observation.id && session_.document == observation.document &&
                    session_.startedAt == observation.startedAt &&
                    session_.utcOffsetMinutes == observation.utcOffsetMinutes;
  uint64_t next = 0;
  const auto result =
      same ? journal_.checkpoint(sessionId_, {bytes, sizeof(bytes)})
           : journal_.enqueue(
                 {0, "native", "local-unbound", "native-session", {bytes, sizeof(bytes)}, {bytes, sizeof(bytes)}, 0},
                 next);
  if (result == JournalResult::Ok) {
    if (!same) sessionId_ = next;
    session_ = observation;
  } else
    LOG_ERR("BSC", "Native session observation unavailable: %u", unsigned(result));
  return result;
}
storage::JournalResult Capture::artifact(std::string_view kind, std::string_view book, const char* path, bool deleted) {
  using storage::JournalResult;
  if (kind.empty() || kind.size() > 32 || book.empty() || book.size() > 1024 || !path || strlen(path) > 1024)
    return JournalResult::Invalid;
  auto file = deleted ? FsFile{} : Storage.open(path, O_RDONLY);
  if (!deleted && !file) {
    LOG_ERR("BSC", "Cannot read native capture source");
    return JournalResult::IoError;
  }
  const size_t total = deleted ? 0 : file.fileSize();
  file.close();
  constexpr size_t Chunk = 2048;
  if (total > 64 * Chunk) {
    LOG_ERR("BSC", "Native artifact exceeds capture capacity; source retained");
    return JournalResult::Capacity;
  }
  const size_t parts = std::max<size_t>(1, (total + Chunk - 1) / Chunk);
  // Reserve the entire multipart observation before writing its first part.
  // F07 reserves 1024 checkpoint bytes and a 24-byte envelope per record.
  const uint64_t needed = total + parts * (32 + kind.size() + book.size() + 24 + 1024 + 6 + 13 + 15);
  if (parts > 64 || journal_.pending() + parts > journal_.limits().entries ||
      uint64_t(journal_.bytes()) + needed > journal_.limits().bytes) {
    LOG_ERR("BSC", "Capture capacity reached; native source retained");
    return JournalResult::Capacity;
  }
  // One reusable main-task buffer, no per-turn heap churn and no large stack.
  // At most 2048 raw bytes + 1024-byte book path + 32-byte kind + 32-byte header.
  static char buffer[3136];
  uint64_t group = 0;
  for (size_t part = 0; part < parts; ++part) {
    const size_t offset = part * Chunk;
    const size_t count = std::min(Chunk, total - offset);
    memset(buffer, 0, 32);
    memcpy(buffer, "BSA1", 4);
    buffer[4] = deleted;
    buffer[5] = part + 1 == parts;
    putWide(buffer + 6, kind.size(), 2);
    putWide(buffer + 8, book.size(), 2);
    putWide(buffer + 10, group, 8);
    putWide(buffer + 18, offset, 4);
    putWide(buffer + 22, total, 4);
    putWide(buffer + 26, count, 2);
    size_t prefix = 32;
    memcpy(buffer + prefix, kind.data(), kind.size());
    prefix += kind.size();
    memcpy(buffer + prefix, book.data(), book.size());
    prefix += book.size();
    if (count) {
      file = Storage.open(path, O_RDONLY);
      const bool read = file && file.fileSize() == total && file.seekSet(offset) &&
                        file.read(buffer + prefix, count) == static_cast<int>(count);
      file.close();  // Close native source before F07 opens the journal generation.
      if (!read) return JournalResult::IoError;
    }
    uint64_t id = 0;
    const auto result =
        journal_.enqueue({0, "native", "local-unbound", "native-artifact", {buffer, prefix + count}, {}, 0}, id);
    if (result != JournalResult::Ok) {
      LOG_ERR("BSC", "Incomplete native artifact retained: %u", unsigned(result));
      return result;
    }
    if (!group) group = id;
  }
  return JournalResult::Ok;
}
}  // namespace booksync::reader
