// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Memory.h>

#include "PersonalReading.h"
namespace booksync {
struct NativePersonalState {
  Provenance provenance{};
  uint32_t books = 0;
  bool ready = false;
};
enum class NativePassageStep : uint8_t { Record, Skip, End, Missing, Oversized, Error };
struct NativePersonalPorts {
  void* context = nullptr;
  NativePersonalState (*state)(void*) = nullptr;
  // One saved book and one clipping only. No full catalog or book array.
  NativePassageStep (*passage)(void*, uint16_t book, uint16_t clipping, PersonalRecord&) = nullptr;
};
// A worker-owned frozen native source. One fallible 80 KiB ordinal/hash index
// replaces 10k decoded records. Hashes accelerate lookup/rank only; find verifies
// every candidate's FULL identity. No native or account writes, no paint work.
class NativePersonalSource final : public PersonalReadingSource {
 public:
  static constexpr uint32_t MaxRows = PersonalReading::MaxRows;
  explicit NativePersonalSource(NativePersonalPorts ports) : ports_(ports) {}
  bool begin();
  bool tick(uint8_t allowance = 1);
  void retire();
  bool busy() const { return busy_; }
  ReadingScope owner() const override { return {}; }
  PersonalState availability(PersonalKind) const override;
  PersonalStep read(PersonalKind, uint32_t, PersonalRecord&) override;
  PersonalStep find(PersonalKind, std::string_view, uint32_t&) override;

 private:
  struct Entry {
    uint32_t hash;
    uint16_t book, clipping;
  };
  static_assert(sizeof(Entry) == 8);
  NativePersonalPorts ports_{};
  NativePersonalState frozen_{};
  std::unique_ptr<Entry[]> entries_;
  PersonalRecord scratch_{};
  uint32_t count_ = 0;
  uint16_t book_ = 0, clipping_ = 0;
  PersonalState status_ = PersonalState::NotSaved;
  bool busy_ = false, observed_ = false;
  bool unchanged() const;
  bool fail(PersonalState);
  static uint32_t rankHash(std::string_view);
};
static_assert(sizeof(NativePersonalSource) <= 3 * 1024);
}  // namespace booksync
