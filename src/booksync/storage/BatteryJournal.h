// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "LifecycleJournal.h"
#include "booksync/services/Battery.h"
namespace booksync::storage {
// Native device-scoped measurement history, published through F06. Logical
// append-only generations never evict saved rows. No open handle across calls.
using BatteryRecord = battery::Record;
class BatteryJournal {
 public:
  static constexpr uint32_t MaxEvents = 32768, SegmentRows = 64, RowBytes = 20;
  explicit BatteryJournal(const char* directory, uint32_t limit = MaxEvents) : directory_(directory), limit_(limit) {}
  JournalResult recover(bool create = false);
  JournalResult startRecovery(bool create = false);
  // At most one 64-record segment per call; Incomplete means more work.
  JournalResult recoverStep();
  JournalResult append(const BatteryRecord& event);
  JournalResult read(uint32_t row, BatteryRecord& event);
  uint32_t count() const { return count_; }
  bool capped() const { return capped_; }
  bool available() const { return ready_; }
  bool futureVersion() const { return future_; }

 private:
  const char* directory_;
  uint32_t limit_, count_ = 0;
  bool ready_ = false, capped_ = false, uncertain_ = false, future_ = false;
  char current_[128]{}, staging_[132]{}, backup_[132]{};
  uint32_t recoverySegment_ = 0;
  bool recovering_ = false, recoveryTail_ = false;
  bool paths(uint32_t segment);
  PublishedFile select(uint32_t segment, uint32_t& rows);
  static bool validate(FsFile&, void*);
};
}  // namespace booksync::storage
