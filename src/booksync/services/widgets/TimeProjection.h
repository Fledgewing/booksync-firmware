// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/TimeWidgets.h"
namespace booksync {
struct TimeProjection {
  TimeCardState availability = TimeCardState::Ready;
  uint32_t ageDays = 0;
  bool enabled = true, stale = false, twelveHour = false;
};
inline bool startTimeCard(TimeProjection options, TimeCardStatus& status, uint32_t bytes) {
  status = {bytes, options.ageDays, options.enabled ? options.availability : TimeCardState::Off, options.stale};
  return status.state == TimeCardState::Ready;
}
}  // namespace booksync
