// SPDX-License-Identifier: GPL-3.0-or-later
#include "BookList.h"

#include <Logging.h>
namespace booksync {
void BookListProjection::clear() {
  static_assert(sizeof(BookRow) <= 256);
  for (auto& book : books) book = {};
  provenance = {};
  request = {};
  total = bytes = count = year = 0;
  state = PersonalState::NotSaved;
  stale = complete = false;
}
namespace {
uint16_t previewRows(WindowSource source) {
  switch (source) {
    case WindowSource::AlsoReading:
    case WindowSource::OnDevice:
      return 3;
    case WindowSource::Recent:
    case WindowSource::WantToRead:
    case WindowSource::AllFinished:
    case WindowSource::PickBackUp:
    case WindowSource::UpNext:
    case WindowSource::FinishedYear:
    case WindowSource::Unopened:
      return 4;
    default:
      return 0;
  }
}
bool rejectBookList(BookListProjection& out, PersonalState state, const char* reason) {
  LOG_ERR("BSBL", "%s", reason);
  const auto request = out.request;
  out.clear();
  out.request = request;
  out.state = state;
  return false;
}
}  // namespace
bool projectBookList(BookListSource source, WindowRequest request, ReadingScope current, uint16_t currentYear,
                     BookListProjection& out) {
  out.clear();
  out.request = request;
  const auto cap = previewRows(request.source);
  if (!cap) {
    out.state = PersonalState::Unsupported;
    return true;
  }
  if (request.offset) return rejectBookList(out, PersonalState::Unavailable, "Preview offset must be zero");
  constexpr uint32_t HeaderBytes = sizeof(BookListProjection) - sizeof(BookListProjection::books);
  if (!request.limit || request.bytes > LibraryBytes || request.bytes < HeaderBytes)
    return rejectBookList(out, PersonalState::Oversized, "Preview allowance exceeds widget budget");
  out.bytes = HeaderBytes;
  if (!source.peek) return true;
  const auto meta = source.peek(source.context, request.source);
  if (meta.provenance.owner &&
      (meta.provenance.owner != current.owner || meta.provenance.accountEpoch != current.epoch))
    return true;
  if (meta.state != PersonalState::Ready && meta.state != PersonalState::Empty) {
    if (meta.state == PersonalState::Oversized || meta.state == PersonalState::Unavailable)
      return rejectBookList(out, meta.state, "Saved source unavailable or oversized");
    out.state = meta.state;
    return true;
  }
  if (meta.state == PersonalState::Empty && (meta.total || meta.saved))
    return rejectBookList(out, PersonalState::Unavailable, "Empty source carries nonempty membership");
  if (!meta.countKnown || (!meta.total && !meta.complete)) return true;
  if (request.source == WindowSource::FinishedYear && (!currentYear || currentYear > 9999 || meta.year != currentYear))
    return true;
  if (meta.saved > meta.total)
    return rejectBookList(out, PersonalState::Unavailable, "Saved membership exceeds authoritative count");
  if (!meta.saved && meta.total) return true;
  const uint16_t count = meta.saved < cap ? static_cast<uint16_t>(meta.saved) : cap;
  const uint32_t charged = HeaderBytes + count * sizeof(BookRow);
  if (count > request.limit || meta.bytes > request.bytes || charged > request.bytes)
    return rejectBookList(out, PersonalState::Oversized, "Preview rows or bytes exceed caller allowance");
  if (count && !source.read) return rejectBookList(out, PersonalState::Unavailable, "Indexed selection reader missing");
  for (uint16_t i = 0; i < count; ++i) {
    auto& row = out.books[i];
    if (!source.read(source.context, request.source, i, row))
      return rejectBookList(out, PersonalState::Unavailable, "Saved selection read failed");
    if (row.title.back() || row.key.provenance != meta.provenance)
      return rejectBookList(out, PersonalState::Unavailable, "Saved row has invalid text or scope");
    for (uint16_t previous = 0; previous < i; ++previous)
      if (out.books[previous].key == row.key)
        return rejectBookList(out, PersonalState::Unavailable, "Saved selection contains a duplicate book");
  }
  if (!(source.peek(source.context, request.source) == meta)) {
    out.clear();  // Retired epoch/generation cannot publish old rows or counts.
    out.request = request;
    return true;
  }
  out.provenance = meta.provenance;
  out.total = meta.total;
  out.count = count;
  out.bytes = charged;
  out.year = request.source == WindowSource::FinishedYear ? currentYear : 0;
  out.stale = meta.stale;
  out.complete = meta.complete;
  out.state = count ? PersonalState::Ready : PersonalState::Empty;
  return true;
}
}  // namespace booksync
