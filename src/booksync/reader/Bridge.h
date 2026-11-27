// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>

#include "Capture.h"
#include "PageCapture.h"
struct ReadingStatsDateTime;
namespace ReadingSessionJournal {
struct Record;
}
namespace booksync::reader {
// Engine integration only. All methods run on the main task with rendering
// excluded. No subscriptions, network, native file mutations or presentation.
class Bridge {
 public:
  Bridge();
  const Sample& lastObservation() const { return capture_.lastSample(); }
  void open(std::string_view path, uint16_t format);
  void startPage(const ReadingStatsDateTime&, uint8_t utcOffset, uint32_t spine, uint32_t page, uint32_t pages,
                 uint32_t fraction, uint32_t scale);
  void locatePage(uint32_t spine, uint32_t page, uint32_t pages, uint32_t fraction, uint32_t scale, uint32_t offset = 0,
                  bool hasOffset = false) {
    pages_.locate(spine, page, pages, fraction, scale, offset, hasOffset);
  }
  void acceptPage(const ReadingStatsDateTime&, uint8_t utcOffset, uint32_t seconds);
  void observeSession(const ReadingSessionJournal::Record& record);
  void observe(Sample sample, const std::string& cache, bool closed = false);
  // Exact, bounded native artifact snapshot. Parts reference the first journal
  // ID; only a terminal part makes a complete snapshot. Native files stay owned
  // by CrossInk. Empty bytes are a deletion observation, never an empty import.
  static storage::JournalResult artifact(std::string_view kind, std::string_view book, const char* path,
                                         bool deleted = false);

 private:
  Capture capture_;
  PageCapture pages_;
  uint8_t pageOffset_ = 255;
};
}  // namespace booksync::reader
