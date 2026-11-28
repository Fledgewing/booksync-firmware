// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "LifecycleProgress.h"
#include "LifecycleState.h"
#include "booksync/reader/Capture.h"
#include "booksync/storage/SourceFile.h"

namespace booksync::server {
// Reused private worker workspace; decoded views borrow the current journal
// entry. Every application proves the pinned F15 lifetime and exact closed
// revision again. No path-based rebinding or reader navigation.
class NativeCaptureGate final {
 public:
  NativeCaptureGate(Account& account, storage::ScopeView scope, storage::SourceFile& file)
      : account_(account), scope_(scope), file_(file), session_(account.snapshot()) {}
  NativeCaptureGate(const NativeCaptureGate&) = delete;
  NativeCaptureGate& operator=(const NativeCaptureGate&) = delete;
  bool verify(const storage::JournalEntry&);
  const reader::OpenCapture& opened() const { return opened_; }
  const reader::Sample& sample() const { return sample_; }
  uint32_t owner() const { return session_.syncOwner; }
  bool pendingProgress() const { return pendingProgress_; }
  bool pendingState() const { return pendingState_; }
  const StateValues& stateReply() const { return state_; }

 private:
  Account& account_;
  storage::ScopeView scope_;
  storage::SourceFile& file_;
  const AccountSnapshot session_;
  reader::OpenCapture opened_{};
  reader::Sample sample_{};
  MatchCheckpoint match_{};
  ProgressValues progress_{};
  StateValues state_{};
  bool pendingProgress_ = false;
  bool pendingState_ = false;
  bool current() const;
};
}  // namespace booksync::server
