// SPDX-License-Identifier: GPL-3.0-or-later
#include <Logging.h>

#include "Collections.h"
namespace booksync {
namespace {
CollectionWidgetState libraryState(PersonalState state) {
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
bool rejectLibraryWidget(LibraryWidgetValues& out, CollectionWidgetState state, const char* reason) {
  LOG_ERR("BSWL", "%s", reason);
  out = {};
  out.state = state;
  return false;
}
}  // namespace
bool projectLibraryWidget(const LibraryWidgetInput& input, ReadingScope current, CollectionAllowance allowance,
                          LibraryWidgetValues& out) {
  out = {};
  if (allowance.rows < 2 || allowance.rows > LibraryRows || allowance.bytes > LibraryBytes ||
      allowance.bytes < sizeof(out) || input.bytes > allowance.bytes)
    return rejectLibraryWidget(out, CollectionWidgetState::Oversize, "Library needs two bounded materialized rows");
  if (input.provenance.owner &&
      (input.provenance.owner != current.owner || input.provenance.accountEpoch != current.epoch))
    return true;
  if (input.state > PersonalState::Oversized)
    return rejectLibraryWidget(out, CollectionWidgetState::Error, "Invalid Library availability");
  out.state = libraryState(input.state);
  if (out.state == CollectionWidgetState::Error || out.state == CollectionWidgetState::Oversize)
    return rejectLibraryWidget(out, out.state, "Library source unavailable or oversized");
  if (out.state != CollectionWidgetState::Ready && out.state != CollectionWidgetState::Empty) return true;
  if (input.state == PersonalState::Empty && input.books.known && input.books.value)
    return rejectLibraryWidget(out, CollectionWidgetState::Error, "Empty Library carries a nonzero total");
  const ReadingValue* counts[] = {&input.books, &input.onCard, &input.authors, &input.series, &input.collections};
  for (const auto* value : counts)
    if (value->known && value->value > UINT32_MAX)
      return rejectLibraryWidget(out, CollectionWidgetState::Oversize, "Library count exceeds its presentation range");
  if (input.storageKnown && input.freeBytes > input.totalBytes)
    return rejectLibraryWidget(out, CollectionWidgetState::Error, "Library free storage exceeds capacity");
  const bool anyKnown = input.books.known || input.onCard.known || input.authors.known || input.series.known ||
                        input.collections.known || input.storageKnown;
  if (!anyKnown) {
    out.state = CollectionWidgetState::NotSaved;
    return true;
  }
  out.provenance = input.provenance;
  out.booksKnown = input.books.known;
  out.books = input.books.known ? static_cast<uint32_t>(input.books.value) : 0;
  out.onCardKnown = input.onCard.known;
  out.onCard = input.onCard.known ? static_cast<uint32_t>(input.onCard.value) : 0;
  out.authorsKnown = input.authors.known;
  out.authors = input.authors.known ? static_cast<uint32_t>(input.authors.value) : 0;
  out.seriesKnown = input.series.known;
  out.series = input.series.known ? static_cast<uint32_t>(input.series.value) : 0;
  out.collectionsKnown = input.collections.known;
  out.collections = input.collections.known ? static_cast<uint32_t>(input.collections.value) : 0;
  out.storageKnown = input.storageKnown;
  out.freeBytes = input.storageKnown ? input.freeBytes : 0;
  out.totalBytes = input.storageKnown ? input.totalBytes : 0;
  out.stale = input.stale;
  out.ageKnown = input.stale && input.ageKnown;
  out.ageDays = out.ageKnown ? input.ageDays : 0;
  out.rows = 2;
  out.bytes = input.bytes > sizeof(out) ? input.bytes : sizeof(out);
  out.state = out.booksKnown && !out.books && out.onCardKnown && !out.onCard ? CollectionWidgetState::Empty
                                                                             : CollectionWidgetState::Ready;
  return true;
}
}  // namespace booksync
