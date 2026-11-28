// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdio>

#include "booksync/ui/Catalog.h"
namespace catalogFixture {
using namespace booksync;
inline constexpr const char* names[] = {"library",
                                        "library-browse",
                                        "library-offline",
                                        "library-missing",
                                        "library-loading",
                                        "library-error",
                                        "authors",
                                        "series-groups",
                                        "on-card",
                                        "lists",
                                        "scope",
                                        "list-page",
                                        "list-back",
                                        "list-empty",
                                        "list-missing",
                                        "list-incomplete",
                                        "list-error",
                                        "lists-unsupported",
                                        "download-options",
                                        "download-offline",
                                        "download-preparing",
                                        "download-progress",
                                        "download-unknown",
                                        "download-failed",
                                        "download-canceled",
                                        "download-unsupported",
                                        "download-login",
                                        "download-space",
                                        "download-access",
                                        "list-saved"};
class SceneLibrary final : public Library {
 public:
  unsigned scene = 0, queries = 0, batches = 0, starts = 0, opens = 0, connects = 0, cancelCalls = 0, resumes = 0,
           detailsCalls = 0, seriesCalls = 0;
  unsigned coverRequests = 0;
  mutable unsigned reads = 0;
  Snapshot owner{};
  CatalogSummary summary{};
  QueryRequest wanted{};
  QueryResult result{};
  LibraryWindow window{};
  CatalogSources sources{};
  DownloadChoices choices{};
  DownloadStatus status{};
  uint64_t sequence = 1;
  SceneLibrary() { configureScene(0); }
  void configureScene(unsigned value) {
    scene = value;
    owner.accountEpoch = 1;
    owner.generation = 1;
    summary = {};
    summary.provenance.accountEpoch = 1;
    summary.provenance.generation = 1;
    summary.publication = ++sequence;
    summary.online = scene != 2 && scene != 19;
    summary.downloadLogin = scene != 26;
    summary.books = {10020, Availability::Ready};
    summary.authors = {2725, Availability::Ready};
    summary.series = {1480, Availability::Ready};
    summary.onCard = {6, Availability::Ready};
    summary.linked = {5, Availability::Ready};
    summary.collections = {1, Availability::Ready};
    summary.lists = {12, Availability::Ready};
    std::snprintf(summary.sourceLabel.data(), summary.sourceLabel.size(), "Saved source");
    if (scene >= 3 && scene <= 5) {
      summary.books.availability = summary.authors.availability = summary.series.availability = Availability::NotSaved;
    }
    choices = {};
    choices.availability = scene == 20   ? Availability::NotSaved
                           : scene == 25 ? Availability::Unsupported
                           : scene == 28 ? Availability::Unavailable
                                         : Availability::Ready;
    choices.count = scene == 25 || scene == 28 ? 0 : 2;
    for (uint8_t i = 0; i < choices.count; ++i) {
      std::snprintf(choices.rows[i].label.data(), choices.rows[i].label.size(), "EPUB %u", unsigned(i + 1));
      choices.rows[i].ordinal = i + 2;
      choices.rows[i].bytes = 2400000;
      choices.rows[i].bytesKnown = i == 0;
    }
    std::snprintf(choices.destination.data(), choices.destination.size(), "Books on card");
    status = {};
    if (scene >= 21 && scene <= 24) {
      status.id = 8;
      status.sequence = ++sequence;
      status.received = 1200000;
      status.total = 2400000;
      status.totalKnown = scene != 22;
      status.state = scene == 23   ? DownloadState::Failed
                     : scene == 24 ? DownloadState::Canceled
                                   : DownloadState::Transferring;
      status.error = scene == 23 ? DownloadError::Unavailable : DownloadError::None;
    }
  }
  Capabilities capabilities() const override { return {true, true, true}; }
  Snapshot snapshot() const override {
    ++reads;
    return owner;
  }
  CatalogSummary catalogSummary() const override {
    ++reads;
    return summary;
  }
  Subscription subscribe() const override { return {sequence, 1, true}; }
  CommandStatus submit(Intent) override { return CommandStatus::Accepted; }
  bool requestBatch(WindowBatchRequest batch) override {
    ++batches;
    window = {};
    window.publication = ++sequence;
    window.provenance = summary.provenance;
    if (scene == 4) return true;
    window.windowCount = batch.count;
    for (uint8_t n = 0; n < batch.count; ++n) {
      auto& span = window.windows[n];
      span.request = batch.windows[n];
      span.first = window.count;
      span.availability = scene == 3   ? Availability::NotSaved
                          : scene == 5 ? Availability::Unavailable
                                       : Availability::Ready;
      span.total = n ? 10020 : 24;
      span.bytes = sizeof(WindowSpan);
      if (span.availability == Availability::Ready)
        for (uint8_t i = 0; i < 4; ++i) {
          auto& book = window.rows[window.count++];
          book.key = {summary.provenance, window.count};
          std::snprintf(book.title.data(), book.title.size(), "Synthetic book %u", unsigned(window.count));
          window.fileAvailability[window.count - 1] = FileAvailability::OnCard;
          ++span.count;
          span.bytes += sizeof(BookRow);
        }
    }
    window.bytes = sizeof(LibraryWindow) - sizeof(BookRow) * LibraryRows + window.count * sizeof(BookRow);
    return true;
  }
  void readWindow(LibraryWindow& out) const override {
    ++reads;
    out = window;
  }
  bool requestQuery(const QueryRequest& q) override {
    ++queries;
    wanted = q;
    result.clear();
    result.requestId = q.requestId;
    result.publication = ++sequence;
    result.provenance = summary.provenance;
    result.availability = scene == 14   ? Availability::NotSaved
                          : scene == 16 ? Availability::Unavailable
                                        : Availability::Ready;
    result.membershipComplete = scene != 15;
    result.matchedBooks = scene == 13 ? 0 : scene == 8 ? 6 : q.scope[0] ? 84 : 10020;
    result.count = scene == 13 || scene == 14 || scene == 16 ? 0 : q.kind == QueryKind::Groups ? 3 : q.limit;
    result.bytes = result.count * sizeof(QueryEntry);
    result.hasMore = result.count > 0;
    for (uint16_t i = 0; i < result.count; ++i) {
      auto& entry = result.entries[i];
      entry.book.key = {summary.provenance, q.offset + i};
      std::snprintf(entry.book.title.data(), entry.book.title.size(), "Synthetic book %u", unsigned(q.offset + i + 1));
      std::snprintf(entry.label.data(), entry.label.size(), "Group %u", unsigned(i + 1));
      std::snprintf(entry.groupKey.data(), entry.groupKey.size(), "group-%u", unsigned(i + 1));
      entry.books = i == 1 ? 1 : 12;
      entry.missing = q.kind == QueryKind::Groups && i == 2;
      entry.onDevice = scene == 8 || i != 1 ? QueryTruth::Yes : QueryTruth::No;
      entry.book.readingKnown = true;
      entry.book.page = 42;
      entry.book.pages = 100;
    }
    return true;
  }
  void readQuery(QueryResult& out) const override {
    ++reads;
    out = result;
  }
  bool requestCatalogSources(CatalogSourceRequest q) override {
    sources = {};
    sources.provenance = summary.provenance;
    sources.requestId = q.requestId;
    sources.publication = ++sequence;
    sources.availability = scene == 17 ? Availability::Unsupported : Availability::Ready;
    sources.count = scene == 17 ? 0 : 6;
    sources.total = 12;
    sources.hasMore = scene != 17;
    for (uint8_t i = 0; i < sources.count; ++i) {
      std::snprintf(sources.rows[i].key.data(), sources.rows[i].key.size(), "scope-%u", unsigned(i + q.offset));
      std::snprintf(sources.rows[i].title.data(), sources.rows[i].title.size(), "Saved list %u",
                    unsigned(i + q.offset + 1));
      sources.rows[i].books = {84, Availability::Ready};
    }
    return true;
  }
  void readCatalogSources(CatalogSources& out) const override {
    ++reads;
    out = sources;
  }
  void readDownloadChoices(BookKey key, DownloadChoices& out) const override {
    ++reads;
    out = choices;
    out.key = key;
  }
  DownloadStatus bookDownloadStatus(BookKey) const override {
    ++reads;
    return status;
  }
  CommandStatus connectForDownload(BookKey) override {
    ++connects;
    return CommandStatus::Accepted;
  }
  CommandStatus download(BookKey, uint8_t) override {
    ++starts;
    return CommandStatus::Accepted;
  }
  CommandStatus requestCover(BookKey) override {
    ++coverRequests;
    return CommandStatus::Accepted;
  }
  CommandStatus open(BookKey) override {
    ++opens;
    return CommandStatus::Accepted;
  }
  CommandStatus cancelDownload(uint64_t) override {
    ++cancelCalls;
    return CommandStatus::Accepted;
  }
  CommandStatus resumeDownload(uint64_t) override {
    ++resumes;
    return CommandStatus::Accepted;
  }
  CommandStatus showBookDetails(BookKey) override {
    ++detailsCalls;
    return CommandStatus::Accepted;
  }
  CommandStatus showCatalogSeries(std::array<char, 64>) override {
    ++seriesCalls;
    return CommandStatus::Accepted;
  }
};
inline void arrange(CatalogSurface& ui, SceneLibrary& library) {
  const auto scene = library.scene;
  if (scene == 1)
    ui.browseCatalogIndex();
  else if (scene == 6)
    ui.browseCatalog(CatalogRoot::Authors);
  else if (scene == 7 || scene == 15)
    ui.browseCatalog(CatalogRoot::Series);
  else if (scene == 8)
    ui.browseCatalog(CatalogRoot::OnCard);
  else if (scene == 9 || scene == 10 || scene == 12 || scene == 17)
    ui.browseCatalog(CatalogRoot::Lists);
  else if (scene >= 11)
    ui.browseCatalog(CatalogRoot::All);
  ui.pollCatalog();
  if (scene == 10 || scene == 12) {
    ui.activateCatalogRow(1);
    ui.pollCatalog();
  }
  if (scene == 11 || scene == 12) {
    ui.pageCatalog(1);
    ui.pollCatalog();
  }
  if (scene == 12) {
    ui.selectCatalogControl(1);
    ui.catalogDetails(1);
  }
  if (scene >= 18) {
    ui.activateCatalogRow(1);
    ui.pollCatalog();
  }
  if (scene == 27 || scene == 29) {
    ui.chooseCatalogDownload(0);
    library.status.sequence = ++library.sequence;
    library.status.id = 8;
    library.status.state = scene == 29 ? DownloadState::Complete : DownloadState::Failed;
    library.status.error = scene == 27 ? DownloadError::NoSpace : DownloadError::None;
    if (scene == 29) {
      ui.catalogDetails(1);
    }
    ui.pollCatalog();
  }
}
}  // namespace catalogFixture
