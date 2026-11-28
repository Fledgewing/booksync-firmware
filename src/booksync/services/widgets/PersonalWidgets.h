// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/PersonalWidgets.h"
#include "booksync/services/PersonalReading.h"
namespace booksync {
struct PersonalWidgetAllowance {
  uint16_t rows = 0;
  uint32_t bytes = LibraryBytes;
};
// The worker owns an immutable F39 source set for this capture. This publication
// token retires the whole set, including a same-count generation replacement.
struct PersonalWidgetCapture {
  Provenance provenance{};
  ReadingScope scope{};
  bool stale = false, ageKnown = false;
  uint32_t ageDays = 0;
  bool operator==(const PersonalWidgetCapture&) const = default;
};
bool projectRecommendedWidget(const PersonalPick&, PersonalWidgetCapture, PersonalWidgetAllowance,
                              PersonalWidgetValues& out);
// Reuses one existing F39 workspace, never four personal engines or UI reads.
// One tick consumes at most 32 source steps. The four consecutive F39 ranks
// wrap through the authoritative pool once, so small pools have no duplicates.
class DiscoverProjection {
 public:
  DiscoverProjection(PersonalReading& personal, PersonalWidgetValues& out) : personal_(personal), out_(out) {}
  bool begin(int64_t day, int64_t pick, PersonalWidgetCapture, PersonalWidgetAllowance);
  bool tick(PersonalWidgetCapture current, uint16_t allowance = PersonalReading::Batch);
  bool busy() const { return busy_; }

 private:
  PersonalReading& personal_;
  PersonalWidgetValues& out_;
  PersonalWidgetCapture capture_{};
  int64_t day_ = 0, pick_ = 0;
  uint32_t total_ = 0;
  uint8_t selected_ = 0, wanted_ = 4;
  bool busy_ = false;
  bool fail(PersonalWidgetState, const char*);
};
static_assert(sizeof(DiscoverProjection) <= 128);
}  // namespace booksync
