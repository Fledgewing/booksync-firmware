// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <cstdint>
namespace booksync {
// Native accepted-frame publication. Only the render owner changes kind_; a
// main-loop navigation may clear ready_ at any time. No scheduling runs here.
class ReminderFrame {
 public:
  void beginRender() { kind_ = Kind::Unchanged; }
  void readingPage() { kind_ = Kind::Reading; }
  void nonReadingFrame() {
    kind_ = Kind::Other;
    block();
  }
  void finishRender(bool book, bool eligible = true) {
    if (!eligible) {
      block();
      return;
    }
    if (!book || kind_ == Kind::Reading)
      ready_.store(true, std::memory_order_release);
    else if (kind_ == Kind::Other)
      block();
    // A no-op refresh preserves the already visible book page.
  }
  void block() { ready_.store(false, std::memory_order_release); }
  bool ready() const { return ready_.load(std::memory_order_acquire); }

 private:
  enum class Kind : unsigned char { Unchanged, Reading, Other } kind_ = Kind::Unchanged;
  std::atomic<uint32_t> ready_{0};
};
// Only load/store are used. The pinned S3 and rv32imc compilers emit aligned
// word loads/stores plus barriers, with no helper calls. C3 reports the broader
// is_always_lock_free property false because it lacks atomic RMW instructions.
static_assert(sizeof(std::atomic<uint32_t>) == sizeof(uint32_t));
}  // namespace booksync
