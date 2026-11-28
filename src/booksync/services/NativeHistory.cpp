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
  if (entry.document != "native-source") return HistoryStep::Skip;
  reader::OpenCapture opened;
  if (!reader::Capture::decodeOpen(entry.payload, opened)) return HistoryStep::Error;
  bool closed = false;
  if (entry.checkpoint.empty()) return HistoryStep::Skip;
  if (!reader::Capture::decode(entry.checkpoint, visit.sample, closed)) return HistoryStep::Error;
  visit.source = opened.bound ? opened.binding.source : storage::SourceIdentity{};
  visit.path = opened.path;
  visit.revision = id;
  return HistoryStep::Record;
}
}  // namespace booksync
