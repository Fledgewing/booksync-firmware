// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <string_view>

#include "booksync/storage/LifecycleJournal.h"

namespace booksync::reader {
// These are observations, not a second reading timer. Native totals and the
// current native session remain separate so providers cannot add them twice.
struct Sample {
  uint32_t spine = 0, page = 0, pages = 0, sourceOffset = 0;
  uint32_t totalSeconds = 0, sessionSeconds = 0, forwardTurns = 0;
  // Exact native progress slots when present; their sequences/CRC and unknown
  // bytes are distinct from the live page/source observation above.
  std::array<uint8_t, 40> rawProgress{};
  uint8_t primaryBytes = 0, backupBytes = 0;
  uint16_t nativeSessions = 0;
  uint32_t openingTotalSeconds = 0;
  std::array<uint8_t, 16> openingCounter{};
  bool hasSourceOffset = false, hasStats = false;
  std::array<uint8_t, 16> counter{};
  bool operator==(const Sample&) const = default;
};
struct SessionObservation {
  std::array<char, 32> id{}, document{};
  uint64_t startedAt = 0, endedAt = 0;
  uint32_t seconds = 0;
  int16_t utcOffsetMinutes = 0;
  bool operator==(const SessionObservation&) const = default;
};
// Main-task only, under the reader's RenderLock. Never called by paint. This
// local producer has no delivery/ack API and never changes native stores.
class Capture {
 public:
  explicit Capture(storage::LifecycleJournal& journal, uint64_t resume = 0) : journal_(journal), id_(resume) {}
  storage::JournalResult open(std::string_view path, uint16_t nativeFormat);
  storage::JournalResult update(Sample sample, bool closed);
  storage::JournalResult session(const SessionObservation& observation);
  storage::JournalResult artifact(std::string_view kind, std::string_view book, const char* path, bool deleted = false);
  const Sample& lastSample() const { return previous_; }
  bool unchanged(const Sample& sample, bool closed) const {
    return sampled_ && previous_ == sample && closed_ == closed;
  }
  uint64_t id() const { return id_; }
  static bool decode(std::string_view checkpoint, Sample& sample, bool& closed);

 private:
  storage::LifecycleJournal& journal_;
  uint64_t id_ = 0;
  Sample previous_{};
  bool sampled_ = false, closed_ = false;
  SessionObservation session_{};
  uint64_t sessionId_ = 0;
};
}  // namespace booksync::reader
