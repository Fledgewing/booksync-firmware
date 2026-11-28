// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "DownloadJobs.h"
#include "booksync/storage/SeriesQueueStore.h"
namespace booksync {
struct SeriesQueuePorts {
  void* context = nullptr;
  bool (*writable)(void*) = nullptr;  // Same serial worker, outside reading/paint.
  bool (*online)(void*) = nullptr;    // Includes current authentication.
  // Recomputed membership/order/job token AND the frozen exact file choices.
  // Never a UI-provided claim. False requires a new explicit preview/review.
  bool (*reviewCurrent)(void*, const storage::SeriesQueueRecord&) = nullptr;
};
// Four durable queues share the EXISTING single transfer engine. Only one
// caller-owned record workspace is retained. No task, transport or payload heap.
class SeriesQueue {
 public:
  SeriesQueue(storage::SeriesQueueStore&, DownloadJobs&, DownloadSource&, SeriesQueuePorts, storage::ScopeView,
              DeliveryTicket, const char* journalPrefix);
  SeriesQueue(const SeriesQueue&) = delete;
  SeriesQueue& operator=(const SeriesQueue&) = delete;
  bool recover();
  uint64_t start(storage::SeriesQueueRecord& reviewed);
  bool resume(uint64_t id);
  bool cancel(uint64_t id, uint8_t row = SeriesNextMax);
  void tick(DeliveryTicket);
  bool read(uint64_t id, SeriesQueueView& out);
  uint64_t find(const char* series) const;
  bool ready() const { return ready_; }
  bool transferBusy() const { return engine_.busy(); }
  uint64_t revision() const { return revision_; }

 private:
  storage::SeriesQueueStore& store_;
  DownloadJobs& engine_;
  DownloadSource& source_;
  SeriesQueuePorts ports_;
  storage::SeriesQueueIndex index_{};
  storage::SeriesQueueRecord record_{};
  std::array<char, 64> server_{}, account_{};
  std::array<char, 180> prefix_{};
  std::array<std::array<char, 200>, 3> journal_{};
  std::array<bool, 4> requested_{};
  std::array<uint16_t, 4> attempted_{};
  DeliveryTicket owner_{};
  uint64_t revision_ = 1;
  uint8_t slot_ = 4, row_ = SeriesNextMax, cursor_ = 0, bound_ = 4;
  bool valid_ = false, ready_ = false, dirty_ = false, indexDirty_ = false, preparing_ = false, retiring_ = false;
  bool allowed() const;
  bool connected() const;
  bool checkpoint();
  bool bind(uint8_t);
  int locate(uint64_t) const;
  void retire();
  bool applyCancel();
  void queueState(SeriesQueueView&);
  void projectRecord(SeriesQueueView&);
};
static_assert(sizeof(SeriesQueue) <= 16 * 1024);
}  // namespace booksync
