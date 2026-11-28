// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <memory>

#include "LookupHistorySource.h"
namespace booksync {
enum class LookupPoolState : uint8_t { NotSaved, Pending, Ready, Empty, Unavailable, Oversize };
struct LookupPoolValue {
  std::array<char, 385> word{};
  uint64_t revision = 0;
  uint32_t total = 0;
  LookupPoolState state = LookupPoolState::NotSaved;
};
struct LookupPoolPorts {
  void* context = nullptr;
  LookupStep (*next)(void*, LookupEntry&) = nullptr;
  bool (*valid)(void*) = nullptr;
};
// One Home-worker lifetime. Exact UTF-8 words, sorted once by insertion into a
// bounded offset index. No context/date/book identity is fabricated from V1.
class LookupPool {
 public:
  static constexpr uint16_t MaxWords = 2048, ArenaBytes = 32768;
  void begin(LookupPoolPorts, int64_t localDay);
  bool tick();
  void retire();
  bool busy() const { return busy_; }
  const LookupPoolValue& result() const { return value_; }
  size_t retainedAllocationBytes() const { return scratch_ ? sizeof(Scratch) : 0; }

 private:
  struct Scratch {
    std::array<char, ArenaBytes> words{};
    std::array<uint16_t, MaxWords> offsets{};
  };
  // 36 KiB fallible scratch is too large for a task frame and is not useful
  // outside an active saved-pool scan. Allocate once on first real record and
  // release on publication, failure or Home exit; no per-tick allocation churn.
  std::unique_ptr<Scratch> scratch_;
  LookupPoolPorts ports_{};
  LookupPoolValue value_{};
  LookupEntry entry_{};
  uint16_t used_ = 0, count_ = 0;
  int64_t day_ = 0;
  bool busy_ = false;
  inline static uint64_t sequence_ = 0;
  bool end(LookupPoolState);
  bool insert();
};
static_assert(sizeof(LookupPool) <= 1024);
}  // namespace booksync
