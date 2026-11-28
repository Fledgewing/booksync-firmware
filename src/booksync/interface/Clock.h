// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace booksync {
enum class ClockEvidence : uint8_t { Unknown, Synchronized, LiveRtc, Retained };
struct ClockStatus {
  ClockEvidence evidence = ClockEvidence::Unknown;
  bool known() const { return evidence != ClockEvidence::Unknown; }
};
}  // namespace booksync
