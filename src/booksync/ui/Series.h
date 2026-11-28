// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/Library.h"
namespace booksync {
enum class SeriesScene : uint8_t { Page, Preview, Count, Files, Review, Notice, Queue };
// Foreground-only Library controller. Published values are cached before paint;
// no scans, file I/O, transport, credentials or worker references enter UI.
class Series {
 public:
  explicit Series(Library& library) : library_(library) {}
  bool enterSeries(const char* key, BookKey current = {}, bool currentKnown = false);
  auto leaveSeries() -> void;
  auto pollSeries() -> bool;
  auto nextSeriesPage(int direction) -> void;
  auto openSeriesBook(uint8_t row) -> void;
  auto searchSeries() -> void;
  auto downloadSeries() -> void;
  auto retrySeriesPage() -> void;
  bool downloadEnabled() const { return delivery_ && !plan_.membershipIncomplete; }
  bool retryEnabled() const { return !loading_ && members_.availability != Availability::Ready; }
  auto reviewSeries() -> void;
  auto changeSeriesCount() -> void;
  auto appendSeriesCount(char seriesDigitByte) -> bool;
  auto eraseSeriesCount() -> void;
  auto stepSeriesCount(int direction) -> void;
  auto submitSeriesCount() -> void;
  auto chooseSeriesFile(uint8_t) -> void;
  auto startSeriesFiles() -> void;
  auto earlierSeriesGap() -> void;
  auto resumeSeriesQueue() -> void;
  auto cancelSeriesRow(uint8_t) -> void;
  auto cancelSeriesRemaining() -> void;
  auto backSeries() -> void;
  SeriesScene scene() const { return scene_; }
  const QueryResult& members() const { return members_; }
  const SeriesPlanView& plan() const { return plan_; }
  const SeriesFlowView& flow() const { return flow_; }
  const char* countText() const { return count_.data(); }
  bool live() const { return live_; }
  bool loading() const { return loading_; }
  bool pending() const { return pending_; }
  uint32_t offset() const { return query_.offset; }

 private:
  Library& library_;
  QueryRequest query_{};
  SeriesFlowRequest request_{};
  QueryResult members_{};
  SeriesPlanView plan_{};
  SeriesFlowView flow_{}, candidate_{};
  std::array<char, 8> count_{};
  DeliveryTicket owner_{};
  uint32_t generation_ = 0, epoch_ = 0;
  uint64_t counter_ = 0, expectedFlow_ = 0, pageRequestId_ = 0, awaitRevision_ = 0;
  SeriesScene scene_ = SeriesScene::Page;
  bool live_ = false, loading_ = false, pending_ = false, awaiting_ = false, delivery_ = false;
  auto sendSeries(SeriesAction) -> bool;
  bool currentSeries() const;
  auto requestSeriesPage() -> void;
  auto adoptSeriesFlow() -> void;
  auto closeSeriesFlow() -> void;
};
static_assert(sizeof(Series) <= 48 * 1024);
}  // namespace booksync
