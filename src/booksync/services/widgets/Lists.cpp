// SPDX-License-Identifier: GPL-3.0-or-later
#include <Logging.h>

#include "Collections.h"
namespace booksync {
namespace {
CollectionWidgetState listsState(PersonalState state) {
  switch (state) {
    case PersonalState::Ready:
      return CollectionWidgetState::Ready;
    case PersonalState::Empty:
      return CollectionWidgetState::Empty;
    case PersonalState::Off:
      return CollectionWidgetState::Off;
    case PersonalState::Unsupported:
      return CollectionWidgetState::Unsupported;
    case PersonalState::Unavailable:
      return CollectionWidgetState::Error;
    case PersonalState::Oversized:
      return CollectionWidgetState::Oversize;
    default:
      return CollectionWidgetState::NotSaved;
  }
}
bool rejectListsWidget(ListsWidgetValues& out, CollectionWidgetState state, const char* reason) {
  LOG_ERR("BSWS", "%s", reason);
  out.clear();
  out.state = state;
  return false;
}
}  // namespace
bool projectListsWidget(ListsWidgetSource source, ReadingScope current, CollectionAllowance allowance,
                        ListsWidgetValues& out) {
  out.clear();
  constexpr uint32_t HeaderBytes = sizeof(ListsWidgetValues) - sizeof(ListsWidgetValues::entries);
  if (!allowance.rows || allowance.rows == 2 || allowance.rows > 8 || allowance.bytes > LibraryBytes ||
      allowance.bytes < HeaderBytes)
    return rejectListsWidget(out, CollectionWidgetState::Oversize, "Lists preview allowance exceeds widget budget");
  if (!source.peek) return true;
  const auto meta = source.peek(source.context);
  if (meta.provenance.owner &&
      (meta.provenance.owner != current.owner || meta.provenance.accountEpoch != current.epoch))
    return true;
  if (meta.state > PersonalState::Oversized)
    return rejectListsWidget(out, CollectionWidgetState::Error, "Invalid Lists availability");
  out.state = listsState(meta.state);
  if (out.state == CollectionWidgetState::Error || out.state == CollectionWidgetState::Oversize)
    return rejectListsWidget(out, out.state, "Lists source unavailable or oversized");
  if (out.state != CollectionWidgetState::Ready && out.state != CollectionWidgetState::Empty) return true;
  if (meta.state == PersonalState::Empty && (meta.saved || (meta.countKnown && meta.total)))
    return rejectListsWidget(out, CollectionWidgetState::Error, "Empty Lists snapshot carries membership");
  if (meta.countKnown && meta.saved > meta.total)
    return rejectListsWidget(out, CollectionWidgetState::Error, "Saved Lists exceed authoritative count");
  if (!meta.saved && (!meta.countKnown || !meta.complete || meta.total)) {
    out.state = CollectionWidgetState::NotSaved;
    return true;
  }
  const uint8_t cap = allowance.rows < 3 ? allowance.rows : 3;
  const uint8_t count = meta.saved < cap ? static_cast<uint8_t>(meta.saved) : cap;
  const uint32_t bytes = HeaderBytes + count * sizeof(ListWidgetEntry);
  if (meta.bytes > allowance.bytes || bytes > allowance.bytes)
    return rejectListsWidget(out, CollectionWidgetState::Oversize, "Lists rows exceed caller byte allowance");
  if (count && !source.read) return rejectListsWidget(out, CollectionWidgetState::Error, "Saved list reader missing");
  for (uint8_t i = 0; i < count; ++i) {
    auto& entry = out.entries[i];
    if (!source.read(source.context, i, entry))
      return rejectListsWidget(out, CollectionWidgetState::Error, "Saved list row read failed");
    if (!entry.name[0] || entry.name.back() || entry.key.provenance != meta.provenance)
      return rejectListsWidget(out, CollectionWidgetState::Error, "Saved list name or scope invalid");
    for (uint8_t j = 0; j < i; ++j)
      if (out.entries[j].key == entry.key)
        return rejectListsWidget(out, CollectionWidgetState::Error, "Duplicate saved-list identity");
    if (!entry.booksKnown) entry.books = 0;
  }
  if (!(source.peek(source.context) == meta)) {
    out.clear();
    return true;
  }
  out.provenance = meta.provenance;
  out.count = count;
  out.countKnown = meta.countKnown;
  out.total = meta.countKnown ? meta.total : 0;
  out.complete = meta.complete;
  out.stale = meta.stale;
  out.ageKnown = meta.stale && meta.ageKnown;
  out.ageDays = out.ageKnown ? meta.ageDays : 0;
  out.bytes = meta.bytes > bytes ? meta.bytes : bytes;
  out.state = count ? CollectionWidgetState::Ready : CollectionWidgetState::Empty;
  return true;
}
bool projectSavedListsPage(ListsWidgetSource source, ReadingScope current, SavedListsRequest request,
                           SavedListsPage& out) {
  out.clear();
  out.offset = request.offset;
  constexpr uint32_t HeaderBytes = sizeof(SavedListsPage) - sizeof(SavedListsPage::entries);
  if (!request.limit || request.limit > SavedListsPage::MaxRows || request.bytes > LibraryBytes ||
      request.bytes < HeaderBytes) {
    LOG_ERR("BSWS", "Saved list page exceeds its allowance");
    out.state = CollectionWidgetState::Oversize;
    return false;
  }
  if (!source.peek) return true;
  const auto meta = source.peek(source.context);
  if (meta.provenance.owner &&
      (meta.provenance.owner != current.owner || meta.provenance.accountEpoch != current.epoch))
    return true;
  if (meta.state > PersonalState::Oversized) {
    LOG_ERR("BSWS", "Invalid saved-list page state");
    out.state = CollectionWidgetState::Error;
    return false;
  }
  out.state = listsState(meta.state);
  if (out.state == CollectionWidgetState::Error || out.state == CollectionWidgetState::Oversize) {
    LOG_ERR("BSWS", "Saved lists source unavailable or oversized");
    return false;
  }
  if (out.state != CollectionWidgetState::Ready && out.state != CollectionWidgetState::Empty) return true;
  if (meta.state > PersonalState::Oversized || (meta.countKnown && meta.saved > meta.total) ||
      (meta.state == PersonalState::Empty && (meta.saved || (meta.countKnown && meta.total)))) {
    LOG_ERR("BSWS", "Saved list page membership inconsistent");
    out.clear();
    out.state = CollectionWidgetState::Error;
    return false;
  }
  if (!meta.saved && (!meta.countKnown || !meta.complete || meta.total)) {
    out.clear();
    return true;
  }
  const uint32_t available = request.offset < meta.saved ? meta.saved - request.offset : 0;
  const uint8_t count = available < request.limit ? static_cast<uint8_t>(available) : request.limit;
  const uint32_t bytes = HeaderBytes + count * sizeof(ListWidgetEntry);
  if (bytes > request.bytes || meta.bytes > request.bytes) {
    LOG_ERR("BSWS", "Saved list page rows exceed byte allowance");
    out.clear();
    out.state = CollectionWidgetState::Oversize;
    return false;
  }
  for (uint8_t i = 0; i < count; ++i) {
    auto& entry = out.entries[i];
    bool valid = source.read && source.read(source.context, request.offset + i, entry);
    valid = valid && entry.name[0] && !entry.name.back() && entry.key.provenance == meta.provenance;
    for (uint8_t j = 0; j < i; ++j)
      if (out.entries[j].key == entry.key) valid = false;
    if (!valid) {
      LOG_ERR("BSWS", "Saved list page row invalid");
      out.clear();
      out.state = CollectionWidgetState::Error;
      return false;
    }
    if (!entry.booksKnown) entry.books = 0;
  }
  if (!(source.peek(source.context) == meta)) {
    out.clear();
    return true;
  }
  out.provenance = meta.provenance;
  out.count = count;
  out.total = meta.countKnown ? meta.total : 0;
  out.saved = meta.saved;
  out.complete = meta.complete;
  out.countKnown = meta.countKnown;
  out.hasMore = request.offset < meta.saved && count < meta.saved - request.offset;
  out.bytes = meta.bytes > bytes ? meta.bytes : bytes;
  out.state =
      meta.countKnown && meta.complete && !meta.total ? CollectionWidgetState::Empty : CollectionWidgetState::Ready;
  return true;
}
}  // namespace booksync
