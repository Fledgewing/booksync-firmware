// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace booksync {
enum class ClockEvidence : uint8_t { Unknown, Synchronized, LiveRtc, Retained };
// Cached civil time uses the configured device timezone. Unknown provenance
// carries no date-shaped fallback, even when the native expiry floor is set.
struct ClockDateTime {
  uint16_t year = 0;
  uint8_t month = 0, day = 0, weekday = 0, hour = 0, minute = 0, second = 0;
};
struct ClockStatus {
  ClockEvidence evidence = ClockEvidence::Unknown;
  int64_t utcSeconds = 0;
  int32_t utcOffsetSeconds = 0;
  ClockDateTime local{};
  bool known() const { return evidence != ClockEvidence::Unknown; }
  bool hasDateTime() const {
    if (!known() || utcSeconds <= 0) return false;
    return local.year != 0;
  }
};
static_assert(sizeof(ClockStatus) <= 32);
}  // namespace booksync
