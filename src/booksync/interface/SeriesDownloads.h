// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/Downloads.h"
#include "booksync/interface/Series.h"
namespace booksync {
enum class SeriesItemState : uint8_t { Queued, Running, Paused, Complete, Canceled, Failed, Unsupported };
struct SeriesQueueItem {
  BookKey key{};
  std::array<char, 96> title{}, destination{};
  std::array<char, 64> variant{};
  uint64_t job = 0, bytes = 0;
  SeriesItemState state = SeriesItemState::Queued;
  DownloadError error = DownloadError::None;
  // Queue rows can be canceled before the transfer engine assigns a job.
  bool sizeKnown = false, cancelWanted = false, cancelable = false;
  void clear() {
    key = {};
    title.fill(0);
    destination.fill(0);
    variant.fill(0);
    job = bytes = 0;
    state = SeriesItemState::Queued;
    error = DownloadError::None;
    sizeKnown = cancelWanted = cancelable = false;
  }
};
struct SeriesQueueView {
  std::array<SeriesQueueItem, SeriesNextMax> items{};
  std::array<char, 64> series{};
  uint64_t id = 0, sequence = 0, received = 0, total = 0;
  uint8_t count = 0, active = 0;
  Availability availability = Availability::NotSaved;
  bool paused = false, complete = false, totalKnown = false;
  void clear() {
    for (uint8_t i = 0; i < SeriesNextMax; ++i) items[i].clear();
    series.fill(0);
    id = sequence = received = total = 0;
    count = active = 0;
    availability = Availability::NotSaved;
    paused = complete = totalKnown = false;
  }
};
static_assert(sizeof(SeriesQueueView) <= 8192);
}  // namespace booksync
