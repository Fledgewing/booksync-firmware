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
static_assert(std::atomic<uint32_t>::is_always_lock_free);
}  // namespace booksync
