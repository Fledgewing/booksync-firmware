// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SeriesPlan.h"
#include "SeriesQueue.h"
#include "booksync/interface/SeriesFlow.h"
namespace booksync {
struct SeriesResolvedFile {
  DownloadSpec selected{};
  std::array<char, 96> label{}, destination{};
};
struct SeriesFileSet {
  std::array<SeriesResolvedFile, SeriesFileChoices> files{};
  uint8_t count = 0;
  bool complete = false;
  void clear();
};
enum class SeriesConnect : uint8_t { Pending, Ready, Failed };
struct SeriesFlowPorts {
  void* context = nullptr;
  bool (*online)(void*) = nullptr;  // Connection only; auth loss still uses the same Resume action.
  bool (*files)(void*, BookKey, SeriesFileSet&) = nullptr;
  bool (*selectionCurrent)(void*, BookKey, const DownloadSpec&) = nullptr;
  bool (*connect)(void*) = nullptr;
  SeriesConnect (*connection)(void*) = nullptr;
  bool (*permitted)(void*) = nullptr;
};
// Called by the sole serialized F23 owner, under its existing main-loop
// admission/network-worker contract. Library owns its intent inbox and
// published view; foreground actions must never call these methods directly.
// One foreground-flow workspace, reused through account lifetime. No task,
// framebuffer, transport owner or second sync/download writer is introduced.
class SeriesFlow {
 public:
  SeriesFlow(SeriesPlan&, SeriesQueue&, SeriesFlowPorts, DeliveryTicket);
  bool action(const SeriesFlowRequest&);
  void tick(DeliveryTicket);
  void copyFlow(SeriesFlowView& out) const;

 private:
  enum class Phase : uint8_t { Idle, Preview, Files, ValidateStart, Start, Queue, ConnectReview, ConnectResume };
  SeriesPlan& planner_;
  SeriesQueue& queue_;
  SeriesFlowPorts ports_;
  DeliveryTicket owner_{};
  SeriesFlowRequest request_{};
  SeriesFlowView view_{};
  storage::SeriesQueueRecord review_{};
  SeriesFileSet options_{};
  uint64_t counter_ = 0, reviewToken_ = 0, reviewCounter_ = 0, choiceCounter_ = 0;
  uint8_t next_ = 0;
  Phase phase_ = Phase::Idle;
  bool live_ = false, valid_ = false, finding_ = false;
  bool sameOwner(DeliveryTicket) const;
  void change(SeriesFlowScene, bool pending = false);
  void notice(SeriesNotice);
  bool beginSeriesPreview(const SeriesRequest&);
  void resolve();
  void selectSeriesOption(uint8_t);
  void pollQueue();
};
static_assert(sizeof(SeriesFlow) <= 48 * 1024);
}  // namespace booksync
