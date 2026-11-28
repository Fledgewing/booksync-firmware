// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "PersonalReading.h"
namespace booksync {
// F39 saved normalized evidence for one frozen current-reader publication.
// Providers verify exact source revision/edition and count only matching timed
// fractional-page visits (1..600 s, valid page/total, start before observation).
// Missing native binding remains absent, never inferred from title/path hashes.
struct ReadingForecast {
  BookRow current{};
  BookKey observed{};
  PersonalCard card{};  // timeLeft is seconds, progress is 0..100, finishDay is a local epoch date.
  uint32_t measuredVisits = 0;
  bool matchingEdition = false;
};
}  // namespace booksync
