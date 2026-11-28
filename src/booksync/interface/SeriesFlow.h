// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/Delivery.h"
#include "booksync/interface/SeriesDownloads.h"
namespace booksync {
constexpr uint8_t SeriesFileChoices = 8;
enum class SeriesFlowScene : uint8_t { Preview, Files, Review, Notice, Queue };
enum class SeriesNotice : uint8_t { ConnectFailed, FileUnavailable, PreviewError, Incomplete, Unavailable, NoEligible };
enum class SeriesAction : uint8_t {
  Begin,
  Preview,
  ConnectAndReview,
  ReviewFiles,
  ChooseFile,
  Start,
  ConnectAndResume,
  Resume,
  CancelRow,
  CancelRemaining,
  Status,
  Close
};
struct SeriesFlowRequest {
  SeriesRequest series{};
  DeliveryTicket owner{};
  uint64_t flow = 0, token = 0, choice = 0, review = 0;
  SeriesAction action = SeriesAction::Begin;
  uint8_t row = 0;
};
struct SeriesFileOption {
  std::array<char, 96> label{};
  uint64_t choice = 0, bytes = 0;
  bool sizeKnown = false;
};
struct SeriesReviewRow {
  std::array<char, 96> label{}, destination{};
  uint64_t bytes = 0;
  bool sizeKnown = false, supported = false;
};
// Neutral published flow values. Exact descriptors and acquisition locators stay
// worker-private; choice is scoped to this flow/token, never a manifest ordinal.
struct SeriesFlowView {
  SeriesPlanView plan{};
  SeriesQueueView queue{};
  std::array<SeriesFileOption, SeriesFileChoices> files{};
  std::array<SeriesReviewRow, SeriesNextMax> review{};
  DeliveryTicket owner{};
  uint64_t flow = 0, revision = 0, requestId = 0, reviewId = 0;
  SeriesFlowScene scene = SeriesFlowScene::Notice;
  SeriesNotice notice = SeriesNotice::Unavailable;
  uint8_t fileCount = 0, fileRow = 0;
  bool online = false, pending = false;
  void clear() {
    plan.clear();
    queue.clear();
    for (uint8_t i = 0; i < SeriesFileChoices; ++i) {
      files[i].label.fill(0);
      files[i].choice = files[i].bytes = 0;
      files[i].sizeKnown = false;
    }
    for (uint8_t i = 0; i < SeriesNextMax; ++i) {
      review[i].label.fill(0);
      review[i].destination.fill(0);
      review[i].bytes = 0;
      review[i].sizeKnown = review[i].supported = false;
    }
    owner = {};
    flow = revision = requestId = reviewId = 0;
    scene = SeriesFlowScene::Notice;
    notice = SeriesNotice::Unavailable;
    fileCount = fileRow = 0;
    online = pending = false;
  }
};
static_assert(sizeof(SeriesFlowView) <= LibraryBytes);
static_assert(sizeof(SeriesFlowRequest) <= 256);
}  // namespace booksync
