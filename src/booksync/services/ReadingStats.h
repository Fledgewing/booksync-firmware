// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>

#include "booksync/storage/PageJournal.h"

namespace booksync {
struct ReadingScope {
  uint64_t owner = 0;
  uint32_t epoch = 1;
  bool operator==(const ReadingScope&) const = default;
};
struct ReadingValue {
  uint64_t value = 0;
  bool known = false;
};
enum class ReadingOrigin : uint8_t { Unknown, ThisReader, Account };
struct ReadingCoverage {
  ReadingOrigin origin = ReadingOrigin::Unknown;
  ReadingScope scope{};
  uint64_t since = 0, through = 0;
  bool saved = false, complete = false;
};
// Exact-snapshot inclusion proof, inclusive by event START time. Fetch/upload
// timestamps and latest accepted tuples are never inclusion evidence.
struct RemoteReadingStats {
  static constexpr uint16_t Days = 365;
  ReadingScope scope{};
  std::array<ReadingValue, Days> days{};
  int64_t firstDay = 0;
  uint16_t dayCount = 0;
  ReadingValue streak{}, bestStreak{}, target{}, finished{}, year{}, includedThrough{};
  uint64_t snapshotAt = 0;
  bool daysAvailable = false, goalAvailable = false, complete = false;
};
enum class StatsStep : uint8_t { Record, End, Error };
struct StatsSource {
  StatsStep (*next)(void*, storage::PageEvent&) = nullptr;
  void* context = nullptr;
  ReadingScope scope{};
  uint64_t since = 0, through = 0;
  bool complete = false;
};
// PageJournal snapshot cursor. Captures a frozen row count; no file remains open.
// Supply explicit coverage evidence from capture; oldest event is NOT evidence
// of uninterrupted capture. Native totals/session observations are not inputs.
class PageStatsSource {
 public:
  explicit PageStatsSource(storage::PageJournal& journal) : journal_(journal) {}
  StatsSource freeze(ReadingScope scope, uint64_t since, uint64_t through, bool complete);

 private:
  storage::PageJournal& journal_;
  uint32_t row_ = 0, count_ = 0;
  static StatsStep next(void*, storage::PageEvent&);
};
struct StatsDaySlice {
  int64_t day = 0;
  uint64_t end = 0;
};
struct StatsHourSlice {
  uint64_t end = 0;
  uint8_t hour = 0;
};
struct StatsCalendar {
  // Device calendar supplies historical DST-aware local dates/boundaries. The
  // fallback is an explicit fixed offset, suitable for UTC/synthetic fixtures.
  bool (*split)(void*, uint64_t, StatsDaySlice&) = nullptr;
  void* context = nullptr;
  bool (*hour)(void*, uint64_t, StatsHourSlice&) = nullptr;
};
struct LocalReadingStats {
  std::array<ReadingValue, RemoteReadingStats::Days> days{};
  ReadingCoverage coverage{};
  ReadingValue today{}, pendingToday{}, recordedPages{}, longestSession{}, streak{}, bestStreak{};
  ReadingValue includedThrough{}, todayPages{}, bestDayPages{};
  std::array<uint64_t, 24> hours{};
  uint32_t hoursKnown = 0;
  bool hoursAvailable = false, hoursComplete = false;
  int64_t firstDay = 0;
};
enum class TodayMerge : uint8_t { LocalOnly, Separate, Watermarked };
struct StatsSnapshot {
  std::array<ReadingValue, RemoteReadingStats::Days> days{};
  ReadingValue today{}, localToday{}, streak{}, bestStreak{}, target{}, year{}, finished{}, includedThrough{};
  ReadingValue recordedPages{}, longestSession{}, nativeToday{}, accountToday{}, todayPages{}, bestDayPages{};
  ReadingCoverage daysCoverage{}, streakCoverage{}, goalCoverage{}, finishedCoverage{}, recordsCoverage{},
      localTodayCoverage{}, nativeTodayCoverage{}, accountTodayCoverage{};
  int64_t firstDay = 0;
  std::array<uint64_t, 24> hours{};
  ReadingCoverage hoursCoverage{};
  uint32_t hoursKnown = 0;
  bool hoursAvailable = false;
  TodayMerge todayMerge = TodayMerge::LocalOnly;
  bool offline = false;
};
class ReadingStats {
 public:
  static constexpr uint16_t Days = RemoteReadingStats::Days, Batch = 32;
  bool begin(StatsSource, uint64_t now, int16_t utcOffsetMinutes, ReadingValue includedThrough,
             StatsCalendar calendar = {});
  bool tick(uint16_t allowance = Batch);
  bool busy() const { return busy_; }
  const LocalReadingStats& local() const { return published_; }
  void combine(const RemoteReadingStats*, ReadingScope current, bool offline, int64_t today, StatsSnapshot&) const;

 private:
  LocalReadingStats pending_{}, published_{};  // ~12 KiB, worker-owned, never a task frame or per-row heap.
  StatsSource source_{};
  StatsCalendar calendar_{};
  storage::PageEvent event_{};
  uint64_t now_ = 0, lastStart_ = 0, unionEnd_ = 0, sessionEnd_ = 0, sessionSeconds_ = 0;
  uint64_t pendingEnd_ = 0, pageRunCount_ = 0;
  int64_t pageRunDay_ = INT64_MIN;
  int64_t lastReadDay_ = INT64_MIN, runStartDay_ = 0, sinceDay_ = 0;
  uint64_t readRun_ = 0;
  bool sinceMidnight_ = false;
  int16_t offset_ = 0;
  bool busy_ = false;
  bool split(uint64_t, StatsDaySlice&) const;
  bool splitHour(uint64_t, StatsHourSlice&) const;
  void markIncomplete();
};
static_assert(sizeof(ReadingStats) <= 13 * 1024);
static_assert(sizeof(StatsSnapshot) <= 8 * 1024);
}  // namespace booksync
