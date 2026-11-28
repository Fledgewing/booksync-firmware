// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Battery.h"
namespace booksync::battery {
struct CaptureInput {
  uint32_t epoch = 0, uptime = 0;
  uint8_t pct = 0;
  uint16_t mv = 0;
  uint8_t lightLevel = 0, content = CONTENT_NONE;
  bool clockKnown = false, known = false, usb = false, radio = false, light = false, crash = false;
};
struct CaptureSink {
  void* context = nullptr;
  bool (*append)(void*, const Record&) = nullptr;
};
// One main-task producer. Input is native checked telemetry, never a paint or a
// second reading timer. Retain an unacknowledged record until F06 confirms it.
class Capture {
 public:
  bool tick(const CaptureInput&, CaptureSink, uint8_t lifecycle = 0);
  bool pending() const { return pending_; }
  bool sleeping() const { return haveLast_ && !pending_ && last_.kind == KIND_SLEEP; }
  void pause() { gap_ = true; }

 private:
  Record last_{}, queued_{};
  uint32_t uptime_ = 0, queuedUptime_ = 0;
  bool haveLast_ = false, pending_ = false, gap_ = false;
  bool publish(Record, uint32_t, CaptureSink);
};
static_assert(sizeof(Capture) <= 64);
}  // namespace booksync::battery
