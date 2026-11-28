// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/Clock.h"
namespace booksync {
enum class TimeWidget : uint8_t { Clock, Moon };
enum class TimeCardState : uint8_t { NotSaved, Ready, Empty, Off, Unsupported, Unavailable, Oversize };
struct TimeCardStatus {
  uint32_t bytes = 0, ageDays = 0;
  TimeCardState state = TimeCardState::NotSaved;
  bool stale = false;
};
struct ClockWidget {
  TimeCardStatus status{sizeof(ClockWidget)};
  ClockStatus reading{};
  bool twelveHour = false;
};
struct MoonWidget {
  TimeCardStatus status{sizeof(MoonWidget)};
  uint32_t nextSeconds = 0;
  int16_t terminator = 1000;  // Projected cosine, scaled by 1000 for the 1-bit symbol.
  uint16_t fraction = 0;      // 0..999 of the estimated mean lunation.
  uint8_t phase = 0, illumination = 0, nextPhase = 0;
};
struct TimeWidgetBatch {
  ClockWidget clock{};
  MoonWidget moon{};
  uint8_t requested = 0;
};
constexpr uint8_t timeWidgetBit(TimeWidget widget) {
  return static_cast<unsigned>(widget) < 2 ? 1u << static_cast<unsigned>(widget) : 0;
}
static_assert(sizeof(TimeWidgetBatch) <= 96);
}  // namespace booksync
