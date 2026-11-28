// SPDX-License-Identifier: GPL-3.0-or-later
#include "ReadModel.h"

#include <Logging.h>

#include "booksync/interface/PersonalWidgets.h"
#include "booksync/interface/StatsWidgets.h"
namespace booksync {
bool ReadModel::accepts(const WindowBatchRequest& batch) {
  constexpr uint32_t Metadata = sizeof(LibraryWindow) - sizeof(BookRow) * LibraryRows;
  if ((!batch.count && !batch.personalWidgets) || batch.personalWidgets > 15 || batch.personalPick > 1000000000 ||
      batch.count > LibraryBatchRequests || !batch.rows || batch.rows > LibraryRows || batch.bytes > LibraryBytes ||
      batch.bytes < Metadata)
    return false;
  constexpr uint16_t StatsMask = (1u << StatsWidgetCount) - 1;
  if (batch.statsWidgets & ~StatsMask) return false;
  const uint32_t statsBytes = batch.statsWidgets ? sizeof(StatsWidgetBatch) : 0;
  uint32_t rows = personalWidgetRows(batch.personalWidgets);
  for (unsigned i = 0; i < batch.count; ++i) {
    const auto request = batch.windows[i];
    if (!request.limit || request.limit > LibraryRows || request.bytes > LibraryBytes ||
        request.bytes < sizeof(WindowSpan) + request.limit * sizeof(BookRow))
      return false;
    rows += request.limit;
  }
  return rows <= batch.rows && Metadata + (rows - personalWidgetRows(batch.personalWidgets)) * sizeof(BookRow) +
                                       statsBytes + personalWidgetBytes(batch.personalWidgets) <=
                                   batch.bytes;
}
bool ReadModel::materialize(WindowRequest request) {
  WindowBatchRequest
      batch;  // Bounded <=288-byte metadata descriptor; avoids a heap allocation and carries no row payload.
  batch.windows[0] = request;
  batch.bytes = request.bytes;
  batch.count = 1;
  return materializeBatch(batch);
}
bool ReadModel::materializeBatch(const WindowBatchRequest& batch) {
  if (!accepts(batch)) return false;  // Entire batch rejected before opening any file.
  constexpr uint32_t Metadata = sizeof(LibraryWindow) - sizeof(BookRow) * LibraryRows;
  historyBusy_ = false;
  window_.rows.fill({});
  window_.fileAvailability.fill(FileAvailability::Unknown);
  window_.windows.fill({});
  window_.count = 0;
  window_.windowCount = batch.count;
  window_.request = batch.windows[0];
  window_.bytes = Metadata + personalWidgetBytes(batch.personalWidgets);
  window_.provenance = index_.provenance();
  window_.total = index_.count();
  window_.stale = false;
  window_.availability = index_.ready() ? Availability::Ready : Availability::NotSaved;
  bool ok = true;
  for (unsigned n = 0; n < batch.count; ++n) {
    auto& span = window_.windows[n];
    span.request = batch.windows[n];
    span.first = window_.count;
    span.total = index_.count();
    span.bytes = sizeof(WindowSpan);
    span.availability = window_.availability;
    if (span.request.source == WindowSource::Continue) {
      span.availability = Availability::Ready;
      span.total = continueAvailable_ ? 1 : 0;
      if (continueAvailable_) {
        window_.rows[window_.count++] = continue_;
        span.count = 1;
        span.bytes += sizeof(BookRow);
        window_.bytes += sizeof(BookRow);
      }
      continue;
    }
    if (span.request.source != WindowSource::OnDevice) {
      span.availability =
          span.request.source == WindowSource::Unknown ? Availability::Unsupported : Availability::NotSaved;
      span.total = 0;
      continue;
    }
    for (uint32_t i = span.request.offset; i < index_.count() && span.count < span.request.limit; ++i) {
      window_.bytes += sizeof(BookRow);
      span.bytes += sizeof(BookRow);  // Failed decodes are charged too.
      if (!index_.summary({window_.provenance, i}, window_.rows[window_.count])) {
        for (unsigned j = span.first; j < window_.count; ++j) window_.rows[j] = {};
        window_.count = span.first;
        span.count = 0;
        span.availability = Availability::Unavailable;
        ok = false;
        break;
      }
      window_.fileAvailability[window_.count] = FileAvailability::OnCard;
      ++span.count;
      ++window_.count;
    }
  }
  if (!ok) window_.availability = Availability::Unavailable;
  publishChanged();
  return ok;
}
void ReadModel::invalidate(Invalidation reason) {
  historyBusy_ = false;
  window_.stale = true;
  if (reason == Invalidation::Account) {
    // Account retirement clears counts/provenance as well as row contents; a
    // cached source total must not escape into the next account's snapshot.
    window_.rows.fill({});
    window_.fileAvailability.fill(FileAvailability::Unknown);
    window_.windows.fill({});
    window_.provenance = {};
    window_.request = {};
    window_.total = window_.bytes = 0;
    window_.windowCount = 0;
    window_.count = 0;
    window_.availability = Availability::NotSaved;
  }
  publishChanged();
}
namespace {
uint64_t pathHash(std::string_view value) {
  uint64_t h = 14695981039346656037ull;
  for (auto c : value) h = (h ^ static_cast<uint8_t>(c)) * 1099511628211ull;
  return h;
}
}  // namespace
bool ReadModel::beginHistory(HistorySource source) {
  if (!source.next || window_.availability != Availability::Ready || source.owner != index_.provenance().owner ||
      source.accountEpoch != index_.provenance().accountEpoch)
    return false;
  historyBusy_ = false;
  historyEnded_ = false;
  history_ = source;
  historyProvenance_ = index_.provenance();
  historyRevision_ = 0;
  reading_.fill({});
  for (unsigned i = 0; i < window_.count; ++i) {
    auto& value = reading_[i];
    const auto& row = window_.rows[i];
    if (row.key.provenance != index_.provenance()) {
      // Continue may carry native state without an indexed identity. Preserve
      // that published value; this index's history cannot replace its meaning.
      value.page = row.page;
      value.pages = row.pages;
      value.seconds = row.seconds;
      value.known = row.readingKnown;
      value.statsKnown = row.statsKnown;
      continue;
    }
    if (!index_.read(row.key, record_)) return false;
    value.hash = pathHash(record_.path.data());
    value.page = record_.page;
    value.pages = record_.pages;
    value.seconds = record_.seconds;
    value.known = record_.readingKnown;
    value.statsKnown = record_.statsKnown;
  }
  historyBusy_ = true;
  return true;
}
bool ReadModel::historyTick(uint16_t allowance) {
  if (!allowance || allowance > HistoryBatch) return false;
  lastHistoryWork_ = 0;
  if (!historyBusy_) return true;
  if (index_.provenance() != historyProvenance_) {
    historyBusy_ = false;
    return false;
  }
  auto& visit = visit_;
  for (; lastHistoryWork_ < allowance; ++lastHistoryWork_) {
    const auto step = historyEnded_ ? HistoryStep::End : history_.next(history_.context, visit);
    if (step == HistoryStep::Error) {
      LOG_ERR("BSI", "Native history projection failed; prior window retained");
      historyBusy_ = false;
      return false;
    }
    if (step == HistoryStep::End) {
      if (lastHistoryWork_) {
        historyEnded_ = true;
        return true;
      }
      historyBusy_ = false;
      for (unsigned i = 0; i < window_.count; ++i) {
        const auto& v = reading_[i];
        auto& row = window_.rows[i];
        row.page = v.page;
        row.pages = v.pages;
        row.seconds = v.seconds;
        row.readingKnown = v.known;
        row.statsKnown = v.statsKnown;
      }
      window_.provenance.capture = historyRevision_;
      window_.stale = false;
      publishChanged();
      return true;
    }
    if (step == HistoryStep::Skip) continue;
    if (visit.path.empty() || visit.path.size() > 1024 || visit.path.front() != '/' || !visit.revision) {
      LOG_ERR("BSI", "Native history record exceeds projection bounds");
      historyBusy_ = false;
      return false;
    }
    const auto hash = pathHash(visit.path);
    if (visit.revision > historyRevision_) historyRevision_ = visit.revision;
    for (unsigned i = 0; i < window_.count; ++i) {
      if (window_.rows[i].key.provenance != historyProvenance_) continue;
      auto& value = reading_[i];
      if (value.hash != hash || (value.known && value.revision >= visit.revision)) continue;
      const auto& row = window_.rows[i];
      if (visit.source.book.bytes != row.book || visit.source.edition.bytes != row.edition ||
          visit.source.revision.bytes != row.revision)
        continue;
      // Hash is only a cheap prefilter, never identity evidence. Verify the exact
      // path before overlaying native display state; it never authorizes replay.
      if (!index_.read(window_.rows[i].key, record_)) {
        historyBusy_ = false;
        return false;
      }
      if (visit.path != record_.path.data()) continue;
      value.revision = visit.revision;
      value.page = visit.sample.page;
      value.pages = visit.sample.pages;
      value.seconds = visit.sample.totalSeconds;
      value.known = true;
      value.statsKnown = visit.sample.hasStats;
    }
  }
  return true;
}
}  // namespace booksync
