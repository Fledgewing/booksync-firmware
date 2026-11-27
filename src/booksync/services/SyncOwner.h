// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>

#include "ServerAdapter.h"

namespace booksync {
enum class SyncResult : uint8_t {
  Ready,
  Busy,
  Inactive,
  Invalid,
  StorageError,
  Idle,
  Advanced,
  Completed,
  Uncertain,
  Unavailable
};
enum class SyncCheckpoint : uint8_t { Activated, AttemptStored, RemoteReturned, AckStored, Finished };
// Optional worker diagnostics; callbacks must not retain borrowed values.
using SyncObserver = void (*)(SyncCheckpoint, uint64_t, unsigned, void*);
// Worker-only. Exactly one activated owner in this process, including across
// different journal objects. Queue identity belongs to the account, not a worker
// lifetime. Scope strings and scratch are borrowed for this object's lifetime.
// All journal users must share the existing external storage serialization contract.
// Destroy only after worker shutdown. No background task or heap allocation here.
class SyncOwner final {
 public:
  static constexpr size_t ScratchBytes = 3 * storage::LifecycleJournal::MaxIdentity +
                                         storage::LifecycleJournal::MaxPayload +
                                         storage::LifecycleJournal::MaxCheckpoint;
  SyncOwner(storage::LifecycleJournal& journal, ServerAdapter& adapter, std::string_view adapterId,
            std::string_view account, char* scratch, size_t capacity, SyncObserver observer = nullptr,
            void* context = nullptr);
  ~SyncOwner();
  SyncOwner(const SyncOwner&) = delete;
  SyncOwner& operator=(const SyncOwner&) = delete;
  SyncResult activate();
  SyncResult deactivate();  // Busy until the borrowed adapter call has returned.
  SyncResult step();        // One phase only. Caller schedules retries, never paint.
 private:
  static std::atomic<SyncOwner*> active_;
  std::atomic_flag busy_ = ATOMIC_FLAG_INIT;
  storage::LifecycleJournal& journal_;
  ServerAdapter& adapter_;
  std::string_view adapterId_, account_;
  char* scratch_;
  size_t capacity_;
  SyncObserver observer_;
  void* context_;
  void observe(SyncCheckpoint point, uint64_t id = 0, unsigned phase = 0) const;
};
}  // namespace booksync
