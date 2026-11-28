// SPDX-License-Identifier: GPL-3.0-or-later
#include "BatteryCapture.h"
namespace booksync::battery {
bool Capture::publish(Record record, uint32_t uptime, CaptureSink sink) {
  queued_ = record;
  queuedUptime_ = uptime;
  pending_ = true;
  if (!sink.append || !sink.append(sink.context, queued_)) return false;
  last_ = record;
  uptime_ = uptime;
  haveLast_ = true;
  pending_ = false;
  return true;
}
bool Capture::tick(const CaptureInput& in, CaptureSink sink, uint8_t lifecycle) {
  if (pending_) {
    const auto kind = queued_.kind;
    if (!publish(queued_, queuedUptime_, sink)) return false;
    gap_ = kind != KIND_GAP;
    return true;
  }
  const bool known = in.known && in.clockKnown && in.epoch >= 946684800 && in.pct <= 100 && in.lightLevel <= 100 &&
                     in.content <= CONTENT_XTC;
  const uint32_t elapsed = in.uptime - uptime_;
  const int64_t clockDelta = static_cast<int64_t>(in.epoch) - last_.t - elapsed;
  const bool discontinuity = haveLast_ && last_.kind != KIND_GAP && known && (clockDelta < -3 || clockDelta > 3);
  if (!known || gap_ || discontinuity) {
    if (!gap_ && !discontinuity && haveLast_ && last_.kind == KIND_GAP) return false;
    Record gap;
    gap.kind = KIND_GAP;
    gap.t = in.clockKnown ? in.epoch : 0;
    gap_ = false;
    return publish(gap, in.uptime, sink);
  }
  Record record;
  record.t = in.epoch;
  record.pct = in.pct;
  record.mv = in.mv;
  record.lightLevel = in.lightLevel;
  record.flags = (in.usb ? FLAG_USB : 0) | (in.radio ? FLAG_RADIO : 0) | (in.light ? FLAG_LIGHT : 0) |
                 (in.content << FLAG_CONTENT_SHIFT);
  if (!haveLast_ || last_.kind == KIND_GAP) {
    record.kind = KIND_BOOT;
    if (in.crash) record.flags |= RESET_CRASH << FLAG_RESET_SHIFT;
  } else if (lifecycle == KIND_SLEEP || lifecycle == KIND_RESTART)
    record.kind = lifecycle;
  else if ((record.flags ^ last_.flags) & FLAG_USB)
    record.kind = KIND_CHARGE;
  else if (record.content() != last_.content())
    record.kind = KIND_CONTENT;
  else if (((record.flags ^ last_.flags) & FLAG_LIGHT) || record.lightLevel != last_.lightLevel)
    record.kind = KIND_LIGHT;
  else if ((record.flags ^ last_.flags) & FLAG_RADIO)
    record.kind = KIND_RADIO;
  else if (record.pct != last_.pct || elapsed >= 60)
    record.kind = KIND_LEVEL;
  else
    return false;
  return publish(record, in.uptime, sink);
}
}  // namespace booksync::battery
