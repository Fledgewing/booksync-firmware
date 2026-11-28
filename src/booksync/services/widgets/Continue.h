// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/ContinueValues.h"
#include "booksync/services/PersonalReading.h"
namespace booksync {
struct ContinueContext {
  std::array<char, 64> seriesLabel{};
  std::array<char, 32> chapterLabel{};
};
// Worker-only projection; F39 already resolves per-field precedence. Native
// position is a compatibility fallback only when resolved progress is unknown.
// Unit/day preparation and unknown-age handling happen before publication.
void projectContinue(bool available, PersonalState, const PersonalCard&, bool stale, uint32_t ageDays,
                     ContinueValues& out, const ContinueContext* context = nullptr, bool ageKnown = true,
                     const BookRow* nativePosition = nullptr);
// Native history updates the position only; other resolved F39 fields remain intact.
void projectContinuePosition(const BookRow&, ContinueValues& out);
}  // namespace booksync
