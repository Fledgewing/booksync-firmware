// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace trustedtime {
enum class ClockSource : uint8_t { Unknown, Sntp, Rtc, Retained };
struct ClockMarker {
  ClockSource source = ClockSource::Unknown;
  int64_t verifiedAt = 0;
};
// Pure policy shared by the clock seam and host fixtures. The caller serializes
// access; a date-shaped number or persisted expiry floor never establishes proof.
class ClockProvenance {
 public:
  static constexpr int64_t MinEpoch = 1735689600LL, MaxEpoch = 4102444800LL;
  static bool plausible(int64_t epoch) { return epoch >= MinEpoch && epoch <= MaxEpoch; }
  void boot(bool deepSleep, ClockMarker retained, int64_t running) {
    marker_ = {};
    if (deepSleep && retained.source != ClockSource::Unknown &&
        static_cast<unsigned>(retained.source) <= static_cast<unsigned>(ClockSource::Retained) &&
        plausible(retained.verifiedAt) && plausible(running) && running >= retained.verifiedAt)
      marker_ = {ClockSource::Retained, retained.verifiedAt};
  }
  void rtc(bool accepted, int64_t epoch) {
    if (accepted && plausible(epoch)) marker_ = {ClockSource::Rtc, epoch};
  }
  void sntp(bool successful, int64_t epoch) {
    if (successful && plausible(epoch)) marker_ = {ClockSource::Sntp, epoch};
  }
  ClockSource status(int64_t running) {
    if (!plausible(running)) marker_ = {};
    return marker_.source;
  }
  ClockMarker marker() const { return marker_; }

 private:
  ClockMarker marker_{};
};
static_assert(sizeof(ClockProvenance) <= 24);
}  // namespace trustedtime
