// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/TimeWidgets.h"
#include "booksync/ui/WidgetHost.h"
namespace booksync::widgets {
enum class TimeLine : uint8_t { Title, Value, Date, TimeSource, TimePhase, Illumination, Next, Stale, State, Retry };
struct TimeMark {
  WidgetCell cell{};
  TimeLine line = TimeLine::Title;
};
struct TimeDrawing {
  std::array<TimeMark, 7> marks{};
  uint8_t count = 0;
  WidgetCell moon{};
};
inline TimeCardState boundedTimeCard(TimeCardStatus status, uint32_t expected) {
  if (status.bytes > expected) return TimeCardState::Oversize;
  if (status.bytes != expected || status.state > TimeCardState::Oversize) return TimeCardState::Unavailable;
  return status.state;
}
inline TimeCardState boundedClockCard(const ClockWidget& timeValue) {
  const auto state = boundedTimeCard(timeValue.status, sizeof(timeValue));
  if (state != TimeCardState::Ready) return state;
  if (!timeValue.reading.hasDateTime()) return TimeCardState::NotSaved;
  const auto civilTime = timeValue.reading.local;
  if (!civilTime.month || civilTime.month > 12 || !civilTime.day || civilTime.day > 31 || civilTime.weekday > 6 ||
      civilTime.hour > 23 || civilTime.minute > 59 || civilTime.second > 59)
    return TimeCardState::Unavailable;
  return state;
}
inline TimeCardState boundedMoonCard(const MoonWidget& timeValue) {
  const auto state = boundedTimeCard(timeValue.status, sizeof(timeValue));
  if (state != TimeCardState::Ready) return state;
  if (timeValue.phase > 7 || timeValue.illumination > 100 || timeValue.fraction > 999 || timeValue.nextPhase > 6 ||
      timeValue.nextPhase % 2 || timeValue.nextSeconds > 638000 || timeValue.terminator < -1000 ||
      timeValue.terminator > 1000)
    return TimeCardState::Unavailable;
  return state;
}
inline void timeLine(TimeDrawing& out, TimeLine line, int x, int y, int w, int h) {
  if (out.count < 7) out.marks[out.count++] = {{x, y, w, h}, line};
}
inline auto timeBoxFits(WidgetBox box) -> bool { return !box.textOnly && box.width >= 180 && box.height >= 104; }
inline bool timeFrame(TimeCardStatus status, uint32_t bytes, WidgetBox box, TimeDrawing& out) {
  out = {};
  if (!timeBoxFits(box)) return false;
  timeLine(out, TimeLine::Title, 10, 6, box.width - 42, 18);
  const auto state = boundedTimeCard(status, bytes);
  if (state != TimeCardState::Ready) {
    timeLine(out, TimeLine::State, 10, 30, box.width - 20, box.height - 40);
    if (state == TimeCardState::Oversize || state == TimeCardState::Unavailable) {
      out.marks[out.count-1].cell.width=box.width-90;
      timeLine(out, TimeLine::Retry, box.width - 70, box.height - 50, 60, 50);
    }
  }
  return true;
}
static_assert(sizeof(TimeDrawing) <= 256);
}  // namespace booksync::widgets
