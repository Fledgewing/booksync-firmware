// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "HistorySource.h"
namespace booksync {
// Read-only F09 source; same serialized main-loop storage owner as capture.
class NativeHistory {
 public:
  bool start();
  HistorySource source() { return {next, this}; }

 private:
  storage::LifecycleJournal journal_{
      {"/.booksync/reader.journal", "/.booksync/reader.journal.tmp", "/.booksync/reader.journal.bak"}};
  uint64_t after_ = 0;
  // Fixed worst-case F07 record (three identities, payload and checkpoint),
  // reused across all visits. Stack unsuitable; no full history retained.
  std::array<char, 3 * storage::LifecycleJournal::MaxIdentity + storage::LifecycleJournal::MaxPayload +
                       storage::LifecycleJournal::MaxCheckpoint>
      bytes_{};
  static HistoryStep next(void*, ReadingVisit&);
};
}  // namespace booksync
