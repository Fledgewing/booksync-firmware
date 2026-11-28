// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#ifdef SIMULATOR
#include <chrono>
#else
#include <esp_timer.h>
#endif
// Boot-local elapsed time includes ESP light sleep, with no 32-bit millis wrap.
// Deep sleep restarts the boot; durable snoozes then use proven wall time.
class HalElapsed {
 public:
  static uint64_t milliseconds() {
#ifdef SIMULATOR
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
#else
    return static_cast<uint64_t>(esp_timer_get_time()) / 1000;
#endif
  }
};
