// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>

#include "Capture.h"
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
  void observeSession(const ReadingSessionJournal::Record& record);
  void observe(Sample sample, const std::string& cache, bool closed = false);
  // Exact, bounded native artifact snapshot. Parts reference the first journal
  // ID; only a terminal part makes a complete snapshot. Native files stay owned
  // by CrossInk. Empty bytes are a deletion observation, never an empty import.
  static storage::JournalResult artifact(std::string_view kind, std::string_view book, const char* path,
                                         bool deleted = false);

 private:
  Capture capture_;
};
}  // namespace booksync::reader
