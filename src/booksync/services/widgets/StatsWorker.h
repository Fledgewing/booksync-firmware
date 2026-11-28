// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Streak.h"
#include "Week.h"
#include "booksync/services/ServerAdapter.h"
namespace booksync {
// Home-lifetime workspace: fixed buffers exceed task stacks and are released
// before reader entry. One bounded cursor tick; no work via public read methods.
class StatsWorker {
 public:
  StatsWorker() : cursor_(journal_) {}
  bool begin(uint16_t requested, Provenance provenance, bool clockKnown, ServerAdapter*);
  bool tick();
  const StatsWidgetBatch& published() const { return published_; }

 private:
  storage::PageJournal journal_{"/.booksync/pages"};
  PageStatsSource cursor_;
  ReadingStats stats_;
  StatsSnapshot snapshot_;
  RemoteReadingStats remote_;
  StatsWidgetBatch published_{};
  StatsProjection options_{};
  ReadingScope scope_{};
  int64_t today_ = 0;
  bool busy_ = false, remoteReady_ = false;
  static bool split(void*, uint64_t, StatsDaySlice&);
  void publish();
};
static_assert(sizeof(StatsWorker) <= 28 * 1024);
}  // namespace booksync
