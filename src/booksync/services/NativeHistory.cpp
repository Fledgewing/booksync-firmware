// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeHistory.h"
namespace booksync {
bool NativeHistory::start() {
  after_ = 0;
  return journal_.recover() == storage::JournalResult::Ok;
}
HistoryStep NativeHistory::next(void* context, ReadingVisit& visit) {
  auto& self = *static_cast<NativeHistory*>(context);
  uint64_t id = 0;
  const auto result = self.journal_.next("native", "local-unbound", self.after_, id);
  if (result == storage::JournalResult::NotFound) return HistoryStep::End;
  if (result != storage::JournalResult::Ok) return HistoryStep::Error;
  self.after_ = id;
  storage::JournalEntry entry;
  if (self.journal_.read(id, entry, self.bytes_.data(), self.bytes_.size()) != storage::JournalResult::Ok)
    return HistoryStep::Error;
  if (entry.payload.size() < 8 || entry.payload.substr(0, 4) != "BSO1") return HistoryStep::Skip;
  const auto n = static_cast<uint8_t>(entry.payload[6]) | (uint16_t(static_cast<uint8_t>(entry.payload[7])) << 8);
  if (entry.payload.size() != size_t(n) + 8 || !n || n > 1024) return HistoryStep::Error;
  bool closed = false;
  if (entry.checkpoint.empty()) return HistoryStep::Skip;
  if (!reader::Capture::decode(entry.checkpoint, visit.sample, closed)) return HistoryStep::Error;
  visit.source = {};
  visit.path = entry.payload.substr(8);
  visit.revision = id;
  return HistoryStep::Record;
}
}  // namespace booksync
