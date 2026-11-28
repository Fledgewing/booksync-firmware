// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <string_view>
namespace booksync {
enum class LookupStep : uint8_t { Pending, Record, EmptyLine, End, Missing, Oversize, Malformed, Changed, IoError };
struct LookupEntry {
  std::array<char, 385> word{};
  uint32_t offset = 0;
  char outcome = 0;  // Native D/T/Y/S/X; absence is not a successful definition.
};
// Bounded capture decoder for the retained reader's existing word|STATUS records. Complete
// native records remain the source; malformed/truncated bytes are never repaired.
class LookupDecoder {
 public:
  LookupStep push(uint8_t byte, LookupEntry& out);
  void reset();
  bool partial() const { return used_ != 0; }
 private:
  std::array<char, 388> line_{};
  uint16_t used_ = 0;
  uint32_t offset_ = 0, lineOffset_ = 0;
  static LookupStep validateWord(std::string_view);
};
// Home-worker-owned reusable scratch (below 2 KiB), never a task frame or paint
// object. One <=256-byte read per tick; every handle is explicitly closed.
// The caller serializes native writers and invalidates on book/index changes.
class LookupHistorySource {
 public:
  static constexpr uint32_t MaxBytes = 1024 * 1024;
  static constexpr uint16_t BatchBytes = 256;
  LookupStep begin(std::string_view cachePath);
  LookupStep tick(LookupEntry& out, uint16_t allowance = BatchBytes);
  bool complete() const { return complete_; }
  bool fromBackup() const { return backup_; }
  uint32_t lookupConsumedBytes() const { return loaded_ - (size_ - pos_); }
 private:
  std::array<char, 1088> path_{};
  std::array<uint8_t, BatchBytes> block_{};
  LookupDecoder decoder_{};
  uint32_t frozen_ = 0, loaded_ = 0;
  uint16_t pos_ = 0, size_ = 0;
  LookupStep state_ = LookupStep::Missing;
  bool complete_ = false, backup_ = false;
  LookupStep fail(LookupStep);
};
static_assert(sizeof(LookupHistorySource) <= 1920);
}  // namespace booksync
