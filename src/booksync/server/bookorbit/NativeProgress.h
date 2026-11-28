// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NativeCaptureGate.h"
#include "ProgressCapture.h"
#include "booksync/services/PositionPolicy.h"

namespace booksync::server {
class NativeProgress final : public ProgressEvidence {
 public:
  static constexpr uint16_t ProtocolLocator = 0x101;
  NativeProgress(Account& account, storage::ScopeView scope, storage::SourceFile& file, ProgressCapture& capture,
                 storage::PositionStore& projection, char* scratch, size_t capacity)
      : gate_(account, scope, file),
        scope_(scope),
        capture_(capture),
        projection_(projection),
        scratch_(scratch),
        capacity_(capacity) {}
  NativeProgress(const NativeProgress&) = delete;
  NativeProgress& operator=(const NativeProgress&) = delete;
  // Sole worker, before any attempt: value is a lossless native mapping (or
  // retained unknown bytes). deliver comes from an explicit neutral F19 intent.
  bool freeze(const storage::JournalEntry&, const storage::PositionValue&, bool deliver, bool wasOverride = false);
  bool load(const storage::JournalEntry&, ProgressInput&) override;
  bool apply(const storage::JournalEntry&, const ProgressInput&, const ProgressValues&) override;

 private:
  NativeCaptureGate gate_;
  storage::ScopeView scope_;
  ProgressCapture& capture_;
  storage::PositionStore& projection_;
  char* scratch_;
  size_t capacity_;
  FrozenProgress frozen_{};
  storage::PositionRecord current_{};
  uint64_t loaded_ = 0;
  bool restoreProjection();
  bool protocol(const storage::PositionValue&) const;
};
}  // namespace booksync::server
