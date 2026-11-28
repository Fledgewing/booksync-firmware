// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ReadingStats.h"
#include "booksync/interface/Models.h"
namespace booksync {
enum class PersonalKind : uint8_t { Highlight, YearAgo, Recommended, Discover };
enum class PersonalState : uint8_t { NotSaved, Ready, Empty, Unsupported, Unavailable, Off, Oversized };
enum class PersonalStep : uint8_t { Record, End, Error };
struct PersonalCard {
  ReadingValue progress{}, timeLeft{}, finishDay{};
  ReadingCoverage progressCoverage{}, timeLeftCoverage{}, finishDayCoverage{};
};
struct PersonalRecord {
  // Exact shared identity (verified path when both catalog/device represent a
  // file), never a hash used as identity evidence. Provider owns neutral ranking.
  std::array<char, 1025> identity{};
  std::array<char, 901> passage{};
  std::array<char, 161> source{};
  BookRow book{};
};
class PersonalReadingSource {
 public:
  virtual ~PersonalReadingSource() = default;
  virtual ReadingScope owner() const = 0;  // owner 0 means native, account-independent data.
  virtual PersonalState availability(PersonalKind) const = 0;
  // Frozen saved snapshot for one batch, deterministic ordinals. No acquisition.
  virtual PersonalStep read(PersonalKind, uint32_t ordinal, PersonalRecord&) = 0;
  // Indexed exact identity lookup returns FIRST ordinal, including duplicates
  // within this provider. It must see the same immutable snapshot as read().
  virtual PersonalStep find(PersonalKind, std::string_view identity, uint32_t& ordinal) = 0;
  virtual bool card(BookKey, PersonalCard&) { return false; }
};
struct PersonalPick {
  PersonalRecord record{};
  ReadingCoverage coverage{};
  PersonalState state = PersonalState::NotSaved;
  uint32_t count = 0;
};
// One worker-owned instance per kind/batch. Providers normalize their saved
// records (F21 catalog, native captures or account snapshots); no UI chooses a
// source. First registered identity/known card field wins, including known zero.
class PersonalReading {
 public:
  static constexpr uint16_t Batch = 32, MaxSources = 8;
  static constexpr uint32_t MaxRows = 10000;
  PersonalReading(PersonalReadingSource* const* sources, uint8_t count) : sources_(sources), count_(count) {}
  bool begin(PersonalKind kind, int64_t localEpochDay, int64_t pick, ReadingScope current);
  bool tick(uint16_t allowance = Batch);
  bool busy() const { return busy_; }
  const PersonalPick& result() const { return result_; }
  bool card(BookKey, ReadingScope current, PersonalCard&);

 private:
  PersonalReadingSource* const* sources_;
  uint8_t count_, source_ = 0, pass_ = 0;
  std::array<bool, MaxSources> enabled_{};
  ReadingScope current_{};
  PersonalKind kind_ = PersonalKind::Highlight;
  PersonalPick result_{};
  PersonalRecord record_{};  // ~2.3 KiB reusable decode scratch, not a task frame.
  PersonalCard card_{};
  std::array<uint32_t, 256> buckets_{};  // Radix selection trades rereads for bounded RAM.
  std::array<uint8_t, 32> prefix_{}, digest_{};
  uint32_t ordinal_ = 0, total_ = 0, target_ = 0, seen_ = 0;
  int64_t day_ = 0, pick_ = 0;
  bool busy_ = false, succeeded_ = false, selecting_ = false, empty_ = false, restarting_ = false;
  bool eligible(unsigned, ReadingScope) const;
  bool unique();
  bool rank();
  bool fail();
  void rejectSource(unsigned);
  void rewind();
  bool endPass();
};
static_assert(sizeof(PersonalReading) <= 7 * 1024);
}  // namespace booksync
