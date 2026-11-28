// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Queries.h"
#include "booksync/interface/Series.h"
namespace booksync {
struct SeriesJobsView {
  void* context = nullptr;
  // Revision changes whenever active/recoverable jobs or their owner changes.
  uint64_t (*revision)(void*) = nullptr;
  QueryTruth (*active)(void*, BookKey) = nullptr;
};
// Worker-only, externally serialized with publication and transfer ownership.
// Exactly two bounded passes; no sorting/selection/I/O in UI or copyPlan().
class SeriesPlan {
 public:
  static constexpr uint16_t Batch = 16;
  SeriesPlan(QuerySource source, SeriesJobsView jobs) : source_(source), jobs_(jobs) {}
  bool request(const SeriesRequest& request);
  bool tick(uint16_t allowance = Batch);
  bool busy() const { return busy_; }
  uint16_t lastWork() const { return lastWork_; }
  void copyPlan(SeriesPlanView& out) const;
  void cancel();

 private:
  QuerySource source_{};
  SeriesJobsView jobs_{};
  QueryState state_{};
  SeriesRequest request_{};
  SeriesPlanView view_{};
  QueryRecord record_{};  // One reused source record, never on the task stack.
  SeriesMember candidate_{}, anchor_{}, reading_{}, finished_{}, explicit_{};
  std::array<char, 40> recent_{};
  uint64_t jobsRevision_ = 0, hash_ = 0;
  uint32_t ordinal_ = 0;
  uint16_t lastWork_ = 0;
  bool busy_ = false, selecting_ = false, currentFound_ = false, startFound_ = false;
  bool readingFound_ = false, finishedFound_ = false, anchorKnown_ = false;
  bool sawRemaining_ = false, allRemainingSaved_ = true;
  bool unchanged() const;
  int membership();
  void project(uint8_t membership);
  void observe();
  void select();
  void finish(Availability availability);
  static int compare(const SeriesMember&, const SeriesMember&);
};
static_assert(sizeof(SeriesPlan) <= 16 * 1024);
}  // namespace booksync
