// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string_view>

#include "FilePublish.h"
namespace booksync::storage {
enum class JournalResult : uint8_t { Ok, Capacity, Invalid, NotFound, Incomplete, RecoveryRequired, IoError };
enum class LifecyclePhase : uint8_t { Match, Stats, Annotations, State, Progress };
struct JournalLimits {
  uint32_t bytes = 256 * 1024;
  uint16_t entries = 64;
};
struct JournalEntry {
  uint64_t id = 0;
  std::string_view adapter, owner, document, payload, checkpoint;
  uint8_t acknowledged = 0;
};
// Worker-only, externally serialized with readers excluded during mutations.
// Paths and entry views are caller-owned. No payload allocations or UI dependency.
// Each append/checkpoint/ack publishes a complete bounded generation via FilePublish.
class LifecycleJournal {
 public:
  static constexpr uint16_t MaxPayload = 4096;
  static constexpr uint16_t MaxCheckpoint = 1024;
  static constexpr uint16_t MaxIdentity = 128;
  static constexpr uint8_t AllPhases = 31;
  explicit LifecycleJournal(PublishPaths paths, JournalLimits limits = {});
  JournalResult recover();  // Read-only; unknown formats block mutation; staging is never replayed.
  JournalResult enqueue(const JournalEntry& entry, uint64_t& id);
  JournalResult checkpoint(uint64_t id, std::string_view bytes);
  JournalResult acknowledge(uint64_t id, LifecyclePhase phase, std::string_view checkpoint);
  JournalResult finish(uint64_t id);  // Caller also proves adapter reconciliation before removal.
  // Returned views expire when the caller-owned bounded buffer is reused.
  JournalResult read(uint64_t id, JournalEntry& entry, char* buffer, size_t capacity);
  JournalResult next(std::string_view adapter, std::string_view owner, uint64_t after, uint64_t& id);
  const JournalLimits& limits() const { return limits_; }
  uint16_t pending() const { return count_; }
  uint32_t bytes() const { return bytes_; }

 private:
  PublishPaths paths_;
  JournalLimits limits_;
  const char* selected_ = nullptr;
  uint64_t nextId_ = 1;
  uint32_t bytes_ = 28;
  uint16_t count_ = 0;
  bool ready_ = false;
  enum class Change { Append, Checkpoint, Acknowledge, Finish };
  JournalResult change(Change kind, uint64_t id, const JournalEntry& entry, uint8_t bit);
  static bool validate(FsFile& file, void*);
};
}  // namespace booksync::storage
