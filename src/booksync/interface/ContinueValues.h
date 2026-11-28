// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
namespace booksync {
enum class ContinueState : uint8_t { NotSaved, Ready, Empty, Unsupported, Error, Off, Oversize };
enum class ContinueTimeUnit : uint8_t { Minutes, Hours };
// One current-book summary per visible-page publication, shared by duplicate
// Continue placements. Unknown values never become zero estimates.
struct ContinueValues {
  std::array<char, 64> seriesLabel{};
  std::array<char, 32> chapterLabel{};
  uint32_t timeLeftAmount = 0;  // Worker-selected display amount/unit.
  uint32_t ageDays = 0;
  ContinueState state = ContinueState::NotSaved;
  ContinueTimeUnit timeLeftUnit = ContinueTimeUnit::Minutes;
  uint8_t finishWeekday = 0, progressPercent = 0;  // Thursday=0; normalized whole percent.
  bool supplied = false;
  bool timeLeftKnown = false, finishDayKnown = false, stale = false, ageKnown = false, progressKnown = false;
};
static_assert(sizeof(ContinueValues) <= 128);
}  // namespace booksync
