#pragma once

#include "booksync/interface/Library.h"

namespace booksync {
// The F04 shell has one bounded local action. Widget layout belongs to F34.
class Home {
  Library& library;

 public:
  explicit Home(Library& library) : library(library) {}
  Snapshot snapshot() const { return library.snapshot(); }
  CommandStatus submit() { return library.submit(Intent::OpenLastBook); }
};
// Once-only presentation consumes cached Library values and submits intents only.
// The caller checks ReadingGate immediately before poll; paint never calls Library.
class UpgradeNotice {
  Library& library_;
  std::array<char, 32> service_{};
  bool visible_ = false, retired_ = false;
  uint8_t focus_ = 0;

 public:
  explicit UpgradeNotice(Library& library) : library_(library) {}
  bool pollNotice(bool mayPresentNotice) {
    if (retired_ || visible_ || !mayPresentNotice) return false;
    const auto noticeSnapshot = library_.snapshot();
    if (!noticeSnapshot.upgradeNoticePending) return false;
    service_ = library_.accountView().serviceLabel;
    service_.back() = 0;
    visible_ = true;
    return true;
  }
  bool visible() const { return visible_; }
  uint8_t focus() const { return focus_; }
  const char* serviceName() const { return service_.data(); }
  void stepNotice(int direction) {
    if (visible_ && focus_ + direction >= 0 && focus_ + direction < 2) focus_ += direction;
  }
  void choose(uint8_t choiceIndex) {
    if (!visible_ || choiceIndex > 1) return;
    visible_ = false;
    retired_ = true;
    library_.submit(Intent::DismissUpgradeNotice);
    if (choiceIndex == 0) library_.submit(Intent::Sync);
  }
  void back() { choose(1); }
};
}  // namespace booksync
