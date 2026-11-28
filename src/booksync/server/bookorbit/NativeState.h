// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NativeCaptureGate.h"
#include "NativeStateStore.h"

namespace booksync::server {
// Concrete F18d evidence and closed-file writeback. Native user-status tokens
// and pending manual intents exclude stale background status; known-history
// flags are stored independently even when remote status cannot apply locally.
class NativeState final : public StateEvidence {
 public:
  NativeState(Account& account, storage::ScopeView scope, storage::SourceFile& file, NativeStateStore& capture,
              NativeStateStore& current)
      : gate_(account, scope, file), scope_(scope), capture_(capture), store_(current) {}
  NativeState(const NativeState&) = delete;
  NativeState& operator=(const NativeState&) = delete;
  bool freeze(const storage::JournalEntry&);
  bool load(const storage::JournalEntry&, StateInput&) override;
  bool apply(const storage::JournalEntry&, const StateInput&, const StateValues&) override;

 private:
  NativeCaptureGate gate_;
  storage::ScopeView scope_;
  NativeStateStore& capture_;
  NativeStateStore& store_;
  NativeStateDocument frozen_{}, current_{};
  std::array<char, 17> capturedToken_{}, currentToken_{};
  uint64_t loaded_ = 0;
  bool restoreCurrent(const storage::SourceIdentity&);
};
}  // namespace booksync::server
