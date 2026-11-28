// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/Library.h"
namespace booksync {
// One foreground owner; input/poll under RenderLock, cached accessors during paint.
class CatalogSurface {
 public:
  enum class Panel : uint8_t { Closed, Dashboard, Browse, Books, Groups, Sources };
  enum class Dialog : uint8_t { None, Offline, Choices, TransferInfo, Login };
  enum class State : uint8_t { Loading, Ready, Empty, NotSaved, Unsupported, Error };
  explicit CatalogSurface(Library& library) : library_(library) {}
  auto enterCatalog(bool textOnly = false) -> void;
  auto retireCatalog() -> void;
  auto pollCatalog() -> bool;
  auto browseCatalog(CatalogRoot root) -> void;
  auto browseCatalogIndex() -> void;
  auto openCatalogShelf(uint8_t index) -> void;
  auto backCatalog() -> bool;
  auto stepCatalog(int direction) -> void;
  auto pageCatalog(int direction) -> bool;
  CommandStatus activateCatalog();
  CommandStatus activateCatalogRow(uint8_t catalogRow);
  CommandStatus activateCatalogShelfBook(uint8_t index);
  CommandStatus activateCatalogGroupBook(uint8_t index);
  CommandStatus chooseCatalogDownload(uint8_t choice);
  CommandStatus catalogDialogAction(uint8_t action);
  CommandStatus catalogDetails(uint8_t catalogRow);
  CommandStatus catalogSeries(uint8_t catalogRow);
  CommandStatus catalogFilter();
  void selectCatalogControl(uint8_t value) {
    if (value < catalogControls()) focus_ = value;
  }
  Panel catalogPanel() const { return panel_; }
  Dialog catalogDialog() const { return dialog_; }
  State catalogState() const { return state_; }
  uint8_t catalogFocus() const { return focus_; }
  auto catalogControls() const -> uint8_t;
  auto catalogVisibleRows() const -> uint8_t;
  uint8_t catalogPageRows() const {
    return panel_ == Panel::Sources ? CatalogSourceRows : panel_ == Panel::Groups ? 3 : textOnly_ ? 6 : 5;
  }
  static CatalogRoot catalogBrowseRoot(uint8_t index);
  CatalogCount catalogRootCount(CatalogRoot root) const;
  CatalogRoot catalogBrowseRootAt(uint8_t index) const;
  uint8_t catalogBrowseCount() const;
  CatalogRoot catalogRoot() const { return current_.root; }
  uint32_t catalogOffset() const { return current_.offset; }
  const CatalogSummary& catalogSummaryValue() const { return summary_; }
  const QueryResult& catalogResult() const { return result_; }
  const CatalogSources& catalogSourcesValue() const { return sources_; }
  const LibraryWindow& catalogCovers() const { return covers_; }
  const DownloadChoices& catalogChoices() const { return choices_; }
  const DownloadStatus& catalogTransferValue() const { return job_; }
  const BookRow& catalogSelectedBook() const { return selected_; }
  CoverReference catalogCover(uint8_t index) const { return references_[index]; }
  uint8_t catalogCoverCount() const { return bitmapCount_; }
  bool catalogAvailabilityUnknown() const { return availabilityUnknown_; }
  bool catalogPreparing() const { return preparing_ || connecting_; }
  DownloadError catalogInlineError() const { return inlineError_; }
  auto catalogTotal() const -> uint32_t;
  auto catalogHasMore() const -> bool;
  bool catalogLive() const { return panel_ != Panel::Closed; }
  static State catalogAvailabilityState(Availability, uint16_t rows);

 private:
  struct Location {
    CatalogRoot root = CatalogRoot::All;
    Panel panel = Panel::Dashboard;
    uint32_t offset = 0;
    uint8_t focus = 0;
    std::array<char, 96> author{};
    std::array<char, 64> scope{}, series{};
    bool withoutSeries = false, withoutAuthor = false;
  } current_{}, history_[4]{};
  // One last-good visible page (not a second catalog/result window). It keeps
  // local rows usable after a same-owner read failure without retaining 24 rows.
  std::array<QueryEntry, 6> retained_{};
  Location retainedLocation_{};
  Provenance retainedProvenance_{};
  uint32_t retainedTotal_ = 0;
  uint8_t retainedCount_ = 0;
  bool retainedComplete_ = false;
  Library& library_;
  CatalogSummary summary_{};
  QueryRequest request_{};  // Reused, never a multi-KiB task-stack temporary.
  QueryResult result_{};
  LibraryWindow covers_{};
  // Dashboard reuses the otherwise idle result entries for eight last-good
  // previews; these descriptors are the only additional cache storage.
  std::array<WindowSpan, 2> retainedShelves_{};
  Provenance retainedShelfProvenance_{};
  uint32_t retainedShelfBytes_ = 0;
  uint8_t retainedShelfRows_ = 0;
  CatalogSources sources_{};
  DownloadChoices choices_{};
  BookRow selected_{};
  DownloadStatus job_{};
  // Cached references only; the host allocates decoded thumbnails on S3 only.
  std::array<CoverReference, 8> references_{};
  Subscription seen_{};
  uint64_t pagePublication_ = 0, expectedDownload_ = 0;
  Panel panel_ = Panel::Closed;
  Dialog dialog_ = Dialog::None;
  State state_ = State::Loading;
  uint32_t accountEpoch_ = 0;
  uint8_t requestedCovers_ = 0;
  bool coverChanged_ = false, preparing_ = false, availabilityUnknown_ = false;
  uint8_t returnFocus_ = 0, focus_ = 0, depth_ = 0, bitmapCount_ = 0;
  bool textOnly_ = false, pending_ = false, queryAccepted_ = false, autoOpen_ = false, connecting_ = false;
  DownloadError inlineError_ = DownloadError::None;
  inline static uint64_t nextRequest_ = 0;
  auto requestCatalogPage() -> void;
  auto pushCatalogLocation() -> bool;
  auto prepareCatalogChoices() -> void;
  auto loadCatalogCovers() -> void;
  auto catalogKeyValid(BookKey) const -> bool;
  auto catalogControlEnabled(uint8_t index) const -> bool;
  CommandStatus selectCatalogBook(const BookRow&, QueryTruth);
};
// Allocated once by the foreground activity and released before reader entry.
static_assert(sizeof(CatalogSurface) <= 32 * 1024);
}  // namespace booksync
