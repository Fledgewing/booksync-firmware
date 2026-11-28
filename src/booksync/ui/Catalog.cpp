// SPDX-License-Identifier: GPL-3.0-or-later
#include "booksync/ui/Catalog.h"
namespace booksync {
namespace {
template <typename Text>
bool catalogTextValid(const Text& value) {
  for (char c : value)
    if (!c) return true;
  return false;
}
}  // namespace
CatalogSurface::State CatalogSurface::catalogAvailabilityState(Availability value, uint16_t rows) {
  if (value == Availability::NotSaved) return State::NotSaved;
  if (value == Availability::Unsupported || value == Availability::Off) return State::Unsupported;
  if (value == Availability::Unavailable) return State::Error;
  return rows ? State::Ready : State::Empty;
}
void CatalogSurface::enterCatalog(bool textOnly, CatalogRoute entry) {
  sourceCursorCount_ = 0;
  sourceOpenPending_ = false;
  entry_ = entry;
  entry_.caption.back() = 0;
  accountEpoch_ = library_.snapshot().accountEpoch;
  textOnly_ = textOnly;
  depth_ = retainedCount_ = retainedShelfRows_ = 0;
  focus_ = 10;
  current_ = {};
  panel_ = Panel::Dashboard;
  dialog_ = Dialog::None;
  summary_ = library_.catalogSummary();
  if (summary_.provenance.accountEpoch != accountEpoch_) {
    summary_ = {};
    summary_.provenance.accountEpoch = accountEpoch_;
    summary_.stale = true;
  }
  summary_.sourceLabel.back() = 0;
  if (entry_.destination == CatalogDestination::BookList) {
    current_.root = CatalogRoot::All;
    panel_ = Panel::Books;
    focus_ = 0;
  }
  requestCatalogPage();
}
void CatalogSurface::retireCatalog() {
  if (sourceOpenPending_) library_.retireOpen(selected_.key);
  sourceOpenPending_ = false;
  if (catalogLive() && request_.requestId) library_.retireQuery(request_.requestId);
  entry_ = {};
  panel_ = Panel::Closed;
  dialog_ = Dialog::None;
  autoOpen_ = connecting_ = preparing_ = false;
  selected_ = {};
  references_.fill({});
  bitmapCount_ = 0;
}
bool CatalogSurface::catalogKeyValid(BookKey key) const {
  return catalogLive() && key.provenance.accountEpoch == accountEpoch_;
}
bool CatalogSurface::pushCatalogLocation() {
  if (depth_ == 4) return false;
  current_.panel = panel_;
  current_.focus = focus_;
  history_[depth_++] = current_;
  return true;
}
void CatalogSurface::requestCatalogPage() {
  if (sourceOpenPending_) library_.retireOpen(selected_.key);
  sourceOpenPending_ = false;
  pending_ = true;
  queryAccepted_ = false;
  state_ = State::Loading;
  seen_ = library_.subscribe();
  inlineError_ = DownloadError::None;
  bitmapCount_ = requestedCovers_ = 0;
  references_.fill({});
  if (panel_ == Panel::Dashboard) {
    // 264 bytes of bounded descriptors, no catalogRow payload on the task stack.
    WindowBatchRequest batch;
    batch.count = 2;
    batch.windows[0].source = WindowSource::Random;
    batch.windows[1].source = WindowSource::Recent;
    batch.windows[0].limit = batch.windows[1].limit = 4;
    queryAccepted_ = library_.requestBatch(batch);
    covers_.windowCount = covers_.count = 0;
  } else if (panel_ == Panel::Sources) {
    request_.requestId = ++nextRequest_;
    sources_.count = 0;
    queryAccepted_ = library_.requestCatalogSources({request_.requestId, current_.offset, current_.root});
  } else if (panel_ == Panel::Browse) {
    queryAccepted_ = true;
  } else {
    request_.filter.authors = {};
    request_.filter.series = {};
    request_.filter.tags = {};
    request_.filter.text.fill(0);
    request_.filter.withoutSeries = current_.withoutSeries;
    request_.filter.withoutAuthor = current_.withoutAuthor;
    request_.filter.group = panel_ == Panel::Groups
                                ? (current_.root == CatalogRoot::Authors ? QueryGroup::Author : QueryGroup::Series)
                                : QueryGroup::None;
    request_.filter.sort = QuerySort::Order;
    request_.filter.where = current_.root == CatalogRoot::OnCard ? QueryWhere::OnDevice : QueryWhere::Anywhere;
    request_.filter.show = current_.root == CatalogRoot::Reading ? QueryShow::Reading : QueryShow::All;
    if (current_.root == CatalogRoot::Recent) request_.filter.sort = QuerySort::Added;
    if (current_.author[0]) {
      request_.filter.authors.values[0] = current_.author;
      request_.filter.authors.count = 1;
    }
    request_.scope = current_.scope;
    request_.seriesKey = current_.series;
    if (current_.series[0]) request_.filter.sort = QuerySort::Series;
    request_.offset = current_.offset;
    request_.requestId = ++nextRequest_;
    request_.limit = catalogPageRows();
    request_.kind = panel_ == Panel::Groups ? QueryKind::Groups : QueryKind::Books;
    result_.clear();
    if (entry_.destination == CatalogDestination::BookList) {
      WindowRequest sourceRequest;
      sourceRequest.source = entry_.source;
      sourceRequest.offset = current_.offset;
      sourceRequest.limit = catalogPageRows();
      queryAccepted_ = library_.requestWindow(sourceRequest);
      if (!queryAccepted_) {
        pending_ = false;
        state_ = State::Unsupported;
      }
      return;
    }
    queryAccepted_ = library_.requestQuery(request_);
  }
  if (!queryAccepted_) {
    pending_ = false;
    state_ = State::Unsupported;
  }
}
void CatalogSurface::browseCatalog(CatalogRoot root) {
  if (!catalogLive() || !pushCatalogLocation()) return;
  retainedShelfRows_ = 0;
  current_ = {};
  current_.root = root;
  panel_ = root == CatalogRoot::Authors || root == CatalogRoot::Series ? Panel::Groups
           : root == CatalogRoot::Lists || root == CatalogRoot::Collections || root == CatalogRoot::Libraries
               ? Panel::Sources
               : Panel::Books;
  focus_ = 0;
  dialog_ = Dialog::None;
  autoOpen_ = false;
  requestCatalogPage();
}
void CatalogSurface::browseCatalogIndex() {
  if (!catalogLive() || !pushCatalogLocation()) return;
  retainedShelfRows_ = 0;
  current_ = {};
  panel_ = Panel::Browse;
  focus_ = 0;
  requestCatalogPage();
}
void CatalogSurface::openCatalogShelf(uint8_t index) {
  if (index == 1)
    browseCatalog(CatalogRoot::Recent);
  else
    browseCatalog(CatalogRoot::All);
}
bool CatalogSurface::backCatalog() {
  if (!catalogLive()) return false;
  autoOpen_ = connecting_ = preparing_ = false;
  if (dialog_ != Dialog::None) {
    dialog_ = Dialog::None;
    focus_ = returnFocus_;
    return true;
  }
  if (!depth_) {
    retireCatalog();
    return true;
  }
  current_ = history_[--depth_];
  panel_ = current_.panel;
  focus_ = current_.focus;
  requestCatalogPage();
  return true;
}
uint8_t CatalogSurface::catalogVisibleRows() const {
  if (panel_ == Panel::Sources) return sources_.count <= CatalogSourceRows ? sources_.count : 0;
  if (panel_ == Panel::Books || panel_ == Panel::Groups) return result_.count <= catalogPageRows() ? result_.count : 0;
  return 0;
}
CatalogRoot CatalogSurface::catalogBrowseRoot(uint8_t index) {
  static constexpr CatalogRoot roots[] = {CatalogRoot::All,    CatalogRoot::Reading,     CatalogRoot::Recent,
                                          CatalogRoot::OnCard, CatalogRoot::Libraries,   CatalogRoot::Authors,
                                          CatalogRoot::Series, CatalogRoot::Collections, CatalogRoot::Lists};
  return index < 9 ? roots[index] : CatalogRoot::All;
}
CatalogCount CatalogSurface::catalogRootCount(CatalogRoot root) const {
  switch (root) {
    case CatalogRoot::Authors:
      return summary_.authors;
    case CatalogRoot::Series:
      return summary_.series;
    case CatalogRoot::OnCard:
      return summary_.onCard;
    case CatalogRoot::Lists:
      return summary_.lists;
    case CatalogRoot::Libraries:
      return summary_.libraries;
    case CatalogRoot::Collections:
      return summary_.collections;
    case CatalogRoot::Reading:
      return summary_.reading;
    default:
      return summary_.books;
  }
}
uint8_t CatalogSurface::catalogBrowseCount() const {
  uint8_t count = 0;
  for (uint8_t i = 0; i < 9; ++i)
    if (catalogRootCount(catalogBrowseRoot(i)).availability != Availability::Unsupported) ++count;
  return count;
}
CatalogRoot CatalogSurface::catalogBrowseRootAt(uint8_t index) const {
  for (uint8_t i = 0; i < 9; ++i) {
    const auto root = catalogBrowseRoot(i);
    if (catalogRootCount(root).availability == Availability::Unsupported) continue;
    if (!index--) return root;
  }
  return CatalogRoot::All;
}
uint8_t CatalogSurface::catalogControls() const {
  if (!catalogLive()) return 0;
  if (dialog_ == Dialog::Choices) return choices_.count + 1;
  if (dialog_ != Dialog::None) return 2;
  if (panel_ == Panel::Dashboard) return 14;  // Shelf headers/books and four compact tiles.
  if (panel_ == Panel::Browse) return catalogBrowseCount();
  if (panel_ == Panel::Groups) return catalogVisibleRows() * 2 + 3;
  return catalogVisibleRows() + (catalogHasMore() ? 3
                                 : current_.offset
                                     ? 2
                                     : 1);  // Filter/retry, previous, catalogNext; stable exits even when empty.
}
bool CatalogSurface::catalogControlEnabled(uint8_t index) const {
  if (index >= catalogControls()) return false;
  if (dialog_ != Dialog::None) return true;
  if (panel_ == Panel::Dashboard && index > 0 && index < 10 && index != 5) {
    const uint8_t shelf = index > 5 ? 1 : 0;
    const uint8_t item = shelf ? index - 6 : index - 1;
    return shelf < covers_.windowCount && item < covers_.windows[shelf].count;
  }
  if (panel_ == Panel::Groups && index >= catalogVisibleRows() + 3) {
    const auto group = index - catalogVisibleRows() - 3;
    return group < result_.count && result_.membershipComplete && result_.entries[group].books > 1;
  }
  if (panel_ != Panel::Dashboard && panel_ != Panel::Browse && index == catalogVisibleRows() + 1 && !current_.offset)
    return false;
  if (panel_ == Panel::Groups && index == catalogVisibleRows() + 2 && !catalogHasMore()) return false;
  return true;
}
void CatalogSurface::stepCatalog(int direction) {
  if (!direction) return;
  int candidate = int(focus_) + (direction > 0 ? 1 : -1);
  while (candidate >= 0 && candidate < catalogControls()) {
    if (catalogControlEnabled(uint8_t(candidate))) {
      focus_ = uint8_t(candidate);
      return;
    }
    candidate += direction > 0 ? 1 : -1;
  }
}
uint32_t CatalogSurface::catalogTotal() const {
  return panel_ == Panel::Sources ? sources_.total : result_.matchedBooks;
}
bool CatalogSurface::catalogHasMore() const { return panel_ == Panel::Sources ? sources_.hasMore : result_.hasMore; }
bool CatalogSurface::pageCatalog(int direction) {
  if (!catalogLive() || dialog_ != Dialog::None ||
      (panel_ != Panel::Sources && panel_ != Panel::Books && panel_ != Panel::Groups))
    return false;
  if (entry_.destination == CatalogDestination::BookList) {
    if (direction > 0) {
      if (!catalogHasMore() || !result_.count || current_.offset > UINT32_MAX - result_.count) return false;
      if (sourceCursorCount_ == sourceCursors_.size()) {
        for (uint8_t i = 1; i < sourceCursorCount_; ++i) sourceCursors_[i - 1] = sourceCursors_[i];
        --sourceCursorCount_;
      }
      sourceCursors_[sourceCursorCount_++] = current_.offset;
      current_.offset += result_.count;
    } else {
      if (!sourceCursorCount_) return false;
      current_.offset = sourceCursors_[--sourceCursorCount_];
    }
    focus_ = 0;
    requestCatalogPage();
    return true;
  }
  const uint32_t limit = panel_ == Panel::Sources ? CatalogSourceRows : catalogPageRows();
  if (direction > 0) {
    if (!catalogHasMore() || current_.offset > UINT32_MAX - limit) return false;
    current_.offset += limit;
  } else {
    if (!current_.offset) return false;
    current_.offset = current_.offset > limit ? current_.offset - limit : 0;
  }
  focus_ = 0;
  autoOpen_ = false;
  requestCatalogPage();
  return true;
}
void CatalogSurface::loadCatalogCovers() {
  coverChanged_ = false;
  if (textOnly_) return;
  const auto count = panel_ == Panel::Dashboard                            ? covers_.count
                     : (panel_ == Panel::Books || panel_ == Panel::Groups) ? result_.count
                                                                           : 0;
  bitmapCount_ = count > 8 ? 8 : uint8_t(count);
  uint8_t admitted = 0;
  for (uint8_t i = 0; i < bitmapCount_; ++i) {
    const auto key = panel_ == Panel::Dashboard ? covers_.rows[i].key : result_.entries[i].book.key;
    if (!catalogKeyValid(key)) continue;
    if (i >= requestedCovers_ && admitted < 4) {
      library_.requestCover(key);
      requestedCovers_ = i + 1;
      ++admitted;
    }
    const auto catalogReference = library_.coverReference(key);
    if (!(references_[i] == catalogReference)) {
      references_[i] = catalogReference;
      coverChanged_ = true;
    }
  }
}
bool CatalogSurface::pollCatalog() {
  if (!catalogLive()) return false;
  if (library_.snapshot().accountEpoch != accountEpoch_) {
    retireCatalog();
    return true;
  }
  bool changed = false;
  const auto nextSummary = library_.catalogSummary();
  if (nextSummary.provenance.accountEpoch != accountEpoch_) {
    changed = summary_.online || summary_.downloadLogin || !summary_.stale;
    summary_.online = summary_.downloadLogin = false;
    summary_.stale = true;
  } else if (nextSummary.publication != summary_.publication || nextSummary.online != summary_.online ||
             nextSummary.downloadLogin != summary_.downloadLogin || nextSummary.stale != summary_.stale) {
    summary_ = nextSummary;
    summary_.sourceLabel.back() = 0;
    changed = true;
  }
  const auto catalogNext = library_.subscribe();
  if (queryAccepted_ &&
      (pending_ || catalogNext.sequence != seen_.sequence || catalogNext.lifetime != seen_.lifetime)) {
    seen_ = catalogNext;
    const auto priorState = state_;
    const bool wasPending = pending_;
    pending_ = false;
    if (panel_ == Panel::Dashboard) {
      library_.readWindow(covers_);
      bool catalogValidWindow = covers_.count <= 8 && covers_.windowCount <= 2 && covers_.bytes <= LibraryBytes &&
                                (!covers_.windowCount || covers_.provenance.accountEpoch == accountEpoch_);
      for (uint8_t n = 0; catalogValidWindow && n < covers_.windowCount; ++n) {
        const auto& span = covers_.windows[n];
        catalogValidWindow = span.count <= 4 && span.first <= covers_.count &&
                             span.count <= covers_.count - span.first && span.bytes <= LibraryBytes &&
                             span.bytes >= sizeof(WindowSpan) + span.count * sizeof(BookRow);
      }
      for (uint16_t n = 0; catalogValidWindow && n < covers_.count; ++n)
        catalogValidWindow = catalogTextValid(covers_.rows[n].title) && catalogKeyValid(covers_.rows[n].key);
      if (!catalogValidWindow) {
        covers_.count = covers_.windowCount = 0;
        state_ = State::Error;
      } else {
        state_ = covers_.windowCount ? State::Ready : State::Loading;
        for (uint8_t n = 0; n < covers_.windowCount; ++n)
          if (covers_.windows[n].availability == Availability::Unavailable) state_ = State::Error;
        if (state_ == State::Ready && covers_.windowCount == 2 &&
            covers_.windows[0].availability == Availability::Ready &&
            covers_.windows[1].availability == Availability::Ready) {
          retainedShelfRows_ = uint8_t(covers_.count);
          retainedShelfProvenance_ = covers_.provenance;
          retainedShelfBytes_ = covers_.bytes;
          for (uint8_t n = 0; n < 2; ++n) retainedShelves_[n] = covers_.windows[n];
          for (uint8_t n = 0; n < retainedShelfRows_; ++n) {
            result_.entries[n].book = covers_.rows[n];
            result_.entries[n].onDevice = covers_.fileAvailability[n] == FileAvailability::OnCard ? QueryTruth::Yes
                                          : covers_.fileAvailability[n] == FileAvailability::ServerOnly
                                              ? QueryTruth::No
                                              : QueryTruth::Unknown;
          }
        } else if (state_ == State::Error && retainedShelfRows_ &&
                   retainedShelfProvenance_.accountEpoch == accountEpoch_) {
          covers_.count = retainedShelfRows_;
          covers_.windowCount = 2;
          covers_.provenance = retainedShelfProvenance_;
          covers_.bytes = retainedShelfBytes_;
          covers_.stale = true;
          for (uint8_t n = 0; n < 2; ++n) covers_.windows[n] = retainedShelves_[n];
          for (uint8_t n = 0; n < retainedShelfRows_; ++n) {
            covers_.rows[n] = result_.entries[n].book;
            covers_.fileAvailability[n] = result_.entries[n].onDevice == QueryTruth::Yes  ? FileAvailability::OnCard
                                          : result_.entries[n].onDevice == QueryTruth::No ? FileAvailability::ServerOnly
                                                                                          : FileAvailability::Unknown;
          }
        }
      }
    } else if (panel_ == Panel::Sources) {
      library_.readCatalogSources(sources_);
      if (sources_.requestId != request_.requestId) {
        pending_ = true;
        return changed;
      }
      if (sources_.count > CatalogSourceRows ||
          (sources_.availability == Availability::Ready && sources_.provenance.accountEpoch != accountEpoch_)) {
        sources_.count = 0;
        state_ = State::Error;
      } else {
        state_ = catalogAvailabilityState(sources_.availability, sources_.count);
        for (uint8_t i = 0; i < sources_.count; ++i)
          if (!catalogTextValid(sources_.rows[i].title) || !catalogTextValid(sources_.rows[i].key)) {
            sources_.count = 0;
            state_ = State::Error;
            break;
          }
      }
    } else if (panel_ == Panel::Browse)
      state_ = State::Ready;
    else {
      if (state_ == State::Ready && result_.count <= 6) {
        retainedLocation_ = current_;
        retainedProvenance_ = result_.provenance;
        retainedTotal_ = result_.matchedBooks;
        retainedComplete_ = result_.membershipComplete;
        retainedCount_ = uint8_t(result_.count);
        for (uint8_t i = 0; i < retainedCount_; ++i) retained_[i] = result_.entries[i];
      }
      if (entry_.destination == CatalogDestination::BookList) {
        // Reuse the existing dashboard window, then feed the approved book-list
        // renderer. Saved ordering and membership remain Library-owned.
        library_.readWindow(covers_);
        result_.clear();
        result_.requestId = request_.requestId;
        result_.publication = covers_.publication;
        if (covers_.windowCount == 1) {
          const auto& sourceSpan = covers_.windows[0];
          const bool sourceValid =
              sourceSpan.request.source == entry_.source &&
              sourceSpan.request.presentation == WindowPresentation::Full &&
              sourceSpan.request.offset == current_.offset && sourceSpan.request.limit == catalogPageRows() &&
              sourceSpan.count <= catalogPageRows() && sourceSpan.first <= covers_.count &&
              sourceSpan.count <= covers_.count - sourceSpan.first && covers_.count <= LibraryRows &&
              covers_.bytes <= LibraryBytes && sourceSpan.bytes <= LibraryBytes;
          result_.availability = sourceValid ? sourceSpan.availability : Availability::Unavailable;
          result_.provenance = covers_.provenance;
          result_.matchedBooks = sourceSpan.total;
          if (sourceValid && sourceSpan.availability == Availability::Ready) {
            result_.count = sourceSpan.count;
            result_.bytes = result_.count * sizeof(QueryEntry);
            result_.hasMore = current_.offset < sourceSpan.total && result_.count < sourceSpan.total - current_.offset;
            for (uint16_t i = 0; i < result_.count; ++i) {
              result_.entries[i].book = covers_.rows[sourceSpan.first + i];
              const auto local = covers_.fileAvailability[sourceSpan.first + i];
              result_.entries[i].onDevice = local == FileAvailability::OnCard       ? QueryTruth::Yes
                                            : local == FileAvailability::ServerOnly ? QueryTruth::No
                                                                                    : QueryTruth::Unknown;
            }
          }
        }
      } else
        library_.readQuery(result_);
      if (result_.requestId != request_.requestId) {
        // A nested catalog owner used the one worker window. Restore this
        // parent's exact query/cursor with a fresh correlation on return.
        request_.requestId = ++nextRequest_;
        queryAccepted_ = library_.requestQuery(request_);
        pending_ = queryAccepted_;
        state_ = queryAccepted_ ? State::Loading : State::Error;
        return true;
      }
      if ((result_.availability == Availability::Ready && result_.provenance.accountEpoch != accountEpoch_) ||
          result_.count > catalogPageRows() || result_.bytes > LibraryBytes ||
          result_.bytes < result_.count * sizeof(QueryEntry)) {
        result_.count = 0;
        state_ = State::Error;
      } else {
        state_ = result_.pending ? State::Loading
                 : result_.availability == Availability::Ready && !result_.membershipComplete
                     ? State::Ready
                     : catalogAvailabilityState(result_.availability, result_.count);
        for (uint16_t i = 0; i < result_.count; ++i)
          if (!catalogTextValid(result_.entries[i].book.title) || !catalogTextValid(result_.entries[i].label) ||
              !catalogTextValid(result_.entries[i].groupKey) ||
              (panel_ == Panel::Books && !catalogKeyValid(result_.entries[i].book.key))) {
            result_.count = 0;
            state_ = State::Error;
            break;
          }
      }
    }
    if (state_ == State::Error && retainedCount_ && retainedProvenance_.accountEpoch == accountEpoch_ &&
        current_.root == retainedLocation_.root && current_.offset == retainedLocation_.offset &&
        current_.scope == retainedLocation_.scope && current_.author == retainedLocation_.author &&
        current_.series == retainedLocation_.series && current_.withoutSeries == retainedLocation_.withoutSeries &&
        current_.withoutAuthor == retainedLocation_.withoutAuthor) {
      result_.count = retainedCount_;
      result_.matchedBooks = retainedTotal_;
      result_.membershipComplete = retainedComplete_;
      result_.provenance = retainedProvenance_;
      for (uint8_t i = 0; i < retainedCount_; ++i) result_.entries[i] = retained_[i];
    }
    const uint64_t publication = panel_ == Panel::Dashboard ? covers_.publication
                                 : panel_ == Panel::Sources ? sources_.publication
                                                            : result_.publication;
    const bool contentChanged = wasPending || state_ != priorState || publication != pagePublication_;
    pagePublication_ = publication;
    if (contentChanged) requestedCovers_ = 0;
    changed = changed || contentChanged;
  }
  if (preparing_) {
    prepareCatalogChoices();
    changed = !preparing_ || changed;
  }
  if (connecting_ && summary_.online) {
    connecting_ = false;
    prepareCatalogChoices();
    changed = true;
  }
  if (selected_.title[0] && catalogKeyValid(selected_.key)) {
    const auto status = library_.bookDownloadStatus(selected_.key);
    if (status.sequence != job_.sequence || status.state != job_.state || status.id != job_.id) {
      const uint64_t unit = status.totalKnown && status.total ? status.total / 20 + 1 : 256 * 1024;
      const bool redraw = status.id != job_.id || status.state != job_.state || status.error != job_.error ||
                          status.totalKnown != job_.totalKnown || status.total != job_.total ||
                          status.received / unit != job_.received / unit;
      job_ = status;
      if (status.state == DownloadState::Failed) inlineError_ = status.error;
      changed = changed || redraw;
      if (autoOpen_ && status.id) {
        if (!expectedDownload_)
          expectedDownload_ = status.id;
        else if (expectedDownload_ != status.id)
          autoOpen_ = false;
      }
      if (status.id && status.state == DownloadState::Complete) {
        for (uint16_t i = 0; i < result_.count; ++i)
          if (result_.entries[i].book.key == selected_.key) result_.entries[i].onDevice = QueryTruth::Yes;
      }
      if (autoOpen_ && expectedDownload_ && status.id == expectedDownload_ && dialog_ == Dialog::None &&
          status.state == DownloadState::Complete) {
        autoOpen_ = false;
        library_.open(selected_.key);
      }
    }
  }
  loadCatalogCovers();
  changed = changed || coverChanged_;
  if (focus_ >= catalogControls()) focus_ = 0;
  return changed;
}
void CatalogSurface::prepareCatalogChoices() {
  library_.readDownloadChoices(selected_.key, choices_);
  preparing_ = choices_.availability == Availability::NotSaved;
  if (preparing_) {
    dialog_ = Dialog::None;
    return;
  }
  if (!(choices_.key == selected_.key) || choices_.count > DownloadChoiceRows ||
      !catalogTextValid(choices_.destination)) {
    choices_.count = 0;
    inlineError_ = DownloadError::Invalid;
    dialog_ = Dialog::None;
    return;
  }
  for (uint8_t i = 0; i < choices_.count; ++i)
    if (!catalogTextValid(choices_.rows[i].label)) {
      choices_.count = 0;
      inlineError_ = DownloadError::Invalid;
      dialog_ = Dialog::None;
      return;
    }
  if (choices_.availability != Availability::Ready || !choices_.count) {
    inlineError_ = DownloadError::Unavailable;
    dialog_ = Dialog::None;
    return;
  }
  dialog_ = Dialog::Choices;
  focus_ = 0;
}
CommandStatus CatalogSurface::activateCatalogRow(uint8_t catalogRow) {
  if (!catalogLive() || dialog_ != Dialog::None || catalogRow >= catalogVisibleRows() ||
      (state_ != State::Ready && state_ != State::Error))
    return CommandStatus::Unavailable;
  if (state_ == State::Error && panel_ == Panel::Books && result_.entries[catalogRow].onDevice != QueryTruth::Yes)
    return CommandStatus::Unavailable;
  if (panel_ == Panel::Sources) {
    focus_ = catalogRow;
    if (!pushCatalogLocation()) return CommandStatus::Unavailable;
    current_.scope = sources_.rows[catalogRow].key;
    current_.offset = focus_ = 0;
    panel_ = Panel::Books;
    requestCatalogPage();
    return CommandStatus::Accepted;
  }
  if (panel_ == Panel::Groups) {
    focus_ = catalogVisibleRows() + 3 + catalogRow;
    if (!pushCatalogLocation()) return CommandStatus::Unavailable;
    const auto& entry = result_.entries[catalogRow];
    if (current_.root == CatalogRoot::Authors) {
      if (entry.missing)
        current_.withoutAuthor = true;
      else
        current_.author = entry.label;
    } else {
      current_.series = entry.groupKey;
      current_.withoutSeries = entry.missing;
    }
    panel_ = Panel::Books;
    current_.offset = focus_ = 0;
    requestCatalogPage();
    return CommandStatus::Accepted;
  }
  focus_ = catalogRow;
  return selectCatalogBook(result_.entries[catalogRow].book, result_.entries[catalogRow].onDevice);
}
CommandStatus CatalogSurface::activateCatalogGroupBook(uint8_t index) {
  if (!catalogLive() || panel_ != Panel::Groups || index >= result_.count ||
      (state_ != State::Ready && state_ != State::Error))
    return CommandStatus::Unavailable;
  if (state_ == State::Error && result_.entries[index].onDevice != QueryTruth::Yes) return CommandStatus::Unavailable;
  focus_ = index;
  return selectCatalogBook(result_.entries[index].book, result_.entries[index].onDevice);
}
CommandStatus CatalogSurface::activateCatalogShelfBook(uint8_t index) {
  if (!catalogLive() || panel_ != Panel::Dashboard || index >= 8) return CommandStatus::Unavailable;
  const uint8_t shelf = index / 4, item = index % 4;
  if (shelf >= covers_.windowCount || item >= covers_.windows[shelf].count) return CommandStatus::Unavailable;
  const auto actual = covers_.windows[shelf].first + item;
  if (actual >= covers_.count) return CommandStatus::Unavailable;
  const auto& book = covers_.rows[actual];
  const auto catalogFileTruth = covers_.fileAvailability[actual];
  if (state_ == State::Error && catalogFileTruth != FileAvailability::OnCard) return CommandStatus::Unavailable;
  return selectCatalogBook(book, catalogFileTruth == FileAvailability::Unknown  ? QueryTruth::Unknown
                                 : catalogFileTruth == FileAvailability::OnCard ? QueryTruth::Yes
                                                                                : QueryTruth::No);
}
CommandStatus CatalogSurface::selectCatalogBook(const BookRow& book, QueryTruth localState) {
  if (!catalogKeyValid(book.key)) return CommandStatus::Unavailable;
  autoOpen_ = connecting_ = preparing_ = false;
  if (sourceOpenPending_) library_.retireOpen(selected_.key);
  sourceOpenPending_ = false;
  selected_ = book;
  availabilityUnknown_ = localState == QueryTruth::Unknown;
  if (availabilityUnknown_) {
    selected_ = book;
    return CommandStatus::Unavailable;
  }
  if (localState == QueryTruth::Yes) {
    if (sourceOpenPending_) library_.retireOpen(selected_.key);
    const auto status = library_.open(book.key);
    sourceOpenPending_ = entry_.destination == CatalogDestination::BookList && status == CommandStatus::Accepted;
    return status;
  }
  returnFocus_ = focus_;
  selected_ = book;
  job_ = library_.bookDownloadStatus(selected_.key);
  autoOpen_ = false;
  inlineError_ = DownloadError::None;
  if (job_.id && job_.state != DownloadState::Complete && job_.state != DownloadState::Missing)
    dialog_ = Dialog::TransferInfo;
  else if (!summary_.downloadLogin)
    dialog_ = Dialog::Login;
  else if (!summary_.online)
    dialog_ = Dialog::Offline;
  else
    prepareCatalogChoices();
  if (dialog_ != Dialog::None) focus_ = 0;
  return CommandStatus::Accepted;
}
CommandStatus CatalogSurface::chooseCatalogDownload(uint8_t index) {
  if (!catalogLive() || dialog_ != Dialog::Choices || index >= choices_.count || !catalogKeyValid(selected_.key))
    return CommandStatus::Unavailable;
  const auto status = library_.download(selected_.key, choices_.rows[index].ordinal);
  if (status == CommandStatus::Accepted) {
    dialog_ = Dialog::None;
    focus_ = returnFocus_;
    autoOpen_ = true;
    expectedDownload_ = 0;
  } else {
    dialog_ = Dialog::None;
    inlineError_ = DownloadError::Unavailable;
  }
  return status;
}
CommandStatus CatalogSurface::catalogDialogAction(uint8_t action) {
  if (!catalogLive() || dialog_ == Dialog::None) return CommandStatus::Unavailable;
  if (dialog_ == Dialog::Choices) {
    if (action < choices_.count) return chooseCatalogDownload(action);
    backCatalog();
    return CommandStatus::Accepted;
  }
  if (dialog_ == Dialog::Login) {
    if (action == 1) {
      dialog_ = Dialog::None;
      return library_.showDownloadAccount();
    }
    backCatalog();
    return CommandStatus::Accepted;
  }
  if (action == 1) {
    backCatalog();
    return CommandStatus::Accepted;
  }
  if (dialog_ == Dialog::Offline) {
    const auto status = library_.connectForDownload(selected_.key);
    dialog_ = Dialog::None;
    connecting_ = status == CommandStatus::Accepted;
    if (!connecting_) inlineError_ = DownloadError::Unavailable;
    return status;
  }
  autoOpen_ = false;
  return job_.state == DownloadState::Failed || job_.state == DownloadState::Paused ||
                 job_.state == DownloadState::Canceled
             ? library_.resumeDownload(job_.id)
             : library_.cancelDownload(job_.id);
}
CommandStatus CatalogSurface::activateCatalog() {
  if (!catalogLive()) return CommandStatus::Unavailable;
  if (dialog_ != Dialog::None) return catalogDialogAction(focus_);
  if (panel_ == Panel::Dashboard) {
    if (focus_ == 0 || focus_ == 5) {
      const uint8_t shelf = focus_ == 5 ? 1 : 0;
      if ((state_ == State::Error && !covers_.windowCount) ||
          (shelf < covers_.windowCount && covers_.windows[shelf].availability == Availability::Unavailable))
        requestCatalogPage();
      else
        openCatalogShelf(shelf);
    } else if (focus_ < 5)
      return activateCatalogShelfBook(focus_ - 1);
    else if (focus_ < 10)
      return activateCatalogShelfBook(focus_ - 2);
    else if (focus_ == 10)
      browseCatalogIndex();
    else if (focus_ < 14)
      browseCatalog(focus_ == 11 ? CatalogRoot::Authors : focus_ == 12 ? CatalogRoot::Series : CatalogRoot::OnCard);
    return CommandStatus::Accepted;
  }
  if (panel_ == Panel::Browse) {
    if (focus_ >= catalogBrowseCount()) return CommandStatus::Unavailable;
    browseCatalog(catalogBrowseRootAt(focus_));
    return CommandStatus::Accepted;
  }
  if (panel_ == Panel::Groups && focus_ >= catalogVisibleRows() + 3)
    return activateCatalogRow(focus_ - catalogVisibleRows() - 3);
  if (focus_ < catalogVisibleRows())
    return panel_ == Panel::Groups ? activateCatalogGroupBook(focus_) : activateCatalogRow(focus_);
  if (focus_ == catalogVisibleRows()) {
    if (state_ == State::Error || state_ == State::NotSaved || state_ == State::Unsupported) {
      requestCatalogPage();
      return CommandStatus::Accepted;
    }
    return catalogFilter();
  }
  return pageCatalog(focus_ == catalogVisibleRows() + 1 ? -1 : 1) ? CommandStatus::Accepted
                                                                  : CommandStatus::Unavailable;
}
CommandStatus CatalogSurface::catalogDetails(uint8_t catalogRow) {
  autoOpen_ = connecting_ = preparing_ = false;
  if (!catalogLive()) return CommandStatus::Unavailable;
  if (panel_ == Panel::Dashboard) {
    if (!catalogRow || catalogRow == 5 || catalogRow >= 10) return CommandStatus::Unavailable;
    const uint8_t shelf = catalogRow > 5 ? 1 : 0;
    const uint8_t item = shelf ? catalogRow - 6 : catalogRow - 1;
    if (shelf >= covers_.windowCount || item >= covers_.windows[shelf].count) return CommandStatus::Unavailable;
    const auto actual = covers_.windows[shelf].first + item;
    if (actual >= covers_.count || !catalogKeyValid(covers_.rows[actual].key)) return CommandStatus::Unavailable;
    focus_ = catalogRow;
    return library_.showBookDetails(covers_.rows[actual].key);
  }
  if ((panel_ != Panel::Books && panel_ != Panel::Groups) || catalogRow >= result_.count ||
      !catalogKeyValid(result_.entries[catalogRow].book.key))
    return CommandStatus::Unavailable;
  focus_ = catalogRow;
  return library_.showBookDetails(result_.entries[catalogRow].book.key);
}
CommandStatus CatalogSurface::catalogSeries(uint8_t catalogRow) {
  autoOpen_ = connecting_ = preparing_ = false;
  if (catalogRow < result_.count) focus_ = catalogRow;
  return catalogLive() && panel_ == Panel::Groups && current_.root == CatalogRoot::Series &&
                 catalogRow < result_.count && !result_.entries[catalogRow].missing
             ? library_.showCatalogSeries(result_.entries[catalogRow].groupKey)
             : CommandStatus::Unavailable;
}
CommandStatus CatalogSurface::catalogFilter() {
  autoOpen_ = connecting_ = preparing_ = false;
  return catalogLive() ? library_.showCatalogFilter() : CommandStatus::Unavailable;
}
}  // namespace booksync
