// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "LifecycleJournal.h"
namespace booksync::storage {
// A local source is the durable F09 open ID, never a guessed canonical identity.
// Original native coordinates remain separate from the whole-book fraction.
struct PageEvent {
  uint64_t open = 0, start = 0;
  uint32_t ordinal = 0, seconds = 0, spine = 0, page = 0, pages = 0, offset = 0;
  uint32_t fraction = 0, scale = 0;
  uint8_t flags = 0;
  static constexpr uint8_t Gap = 1, HasOffset = 2;
  bool operator==(const PageEvent&) const = default;
};
// Externally serialized with F09/F17; no open handle between calls. Logical
// append-only segments use F06 publication and F07 version/CRC/capacity rules.
// No eviction, even after delivery: explicit retention work is a later slice.
class PageJournal {
 public:
  static constexpr uint32_t MaxEvents = 50000, SegmentRows = 64, RowBytes = 64;
  explicit PageJournal(const char* directory, uint32_t limit = MaxEvents) : directory_(directory), limit_(limit) {}
  JournalResult recover();
  JournalResult append(const PageEvent& event);
  JournalResult read(uint32_t row, PageEvent& event);
  uint32_t count() const { return count_; }
  bool capped() const { return capped_; }
  bool available() const { return ready_; }

 private:
  const char* directory_;
  uint32_t limit_, count_ = 0;
  bool ready_ = false, capped_ = false, uncertain_ = false;
  char current_[128]{}, staging_[132]{}, backup_[132]{};
  bool paths(uint32_t segment);
  PublishedFile select(uint32_t segment, uint32_t& rows);
  static bool validate(FsFile&, void*);
};
}  // namespace booksync::storage
