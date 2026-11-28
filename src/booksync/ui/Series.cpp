// SPDX-License-Identifier: GPL-3.0-or-later
#include "booksync/ui/Series.h"

namespace booksync {
namespace {
uint32_t seriesTextLength(const char* seriesTextBytes) {
  uint32_t length = 0;
  while (seriesTextBytes[length]) ++length;
  return length;
}
bool seriesOwner(DeliveryTicket a, DeliveryTicket b) { return a.owner == b.owner && a.epoch == b.epoch; }
}  // namespace
bool Series::currentSeries() const {
  const auto snapshot = library_.snapshot();
  return live_ && snapshot.generation == generation_ && snapshot.accountEpoch == epoch_ &&
         seriesOwner(library_.accountView().ticket, owner_);
}
bool Series::enterSeries(const char* key, BookKey current, bool known) {
  leaveSeries();
  if (!key || !*key || seriesTextLength(key) >= 64) return false;
  for (uint8_t i = 0; i < seriesTextLength(key); ++i) request_.series.key[i] = key[i];
  request_.series.current = current;
  request_.series.currentKnown = known;
  request_.series.count = 3;
  owner_ = library_.accountView().ticket;
  request_.owner = owner_;
  const auto snapshot = library_.snapshot();
  generation_ = snapshot.generation;
  epoch_ = snapshot.accountEpoch;
  delivery_ = library_.capabilities().delivery;
  live_ = true;
  scene_ = SeriesScene::Page;
  query_.seriesKey = request_.series.key;
  query_.filter.sort = QuerySort::Series;
  query_.limit = 3;
  requestSeriesPage();
  return true;
}
void Series::closeSeriesFlow() {
  if (expectedFlow_ || awaiting_) {
    request_.flow = expectedFlow_;
    request_.action = SeriesAction::Close;
    library_.seriesAction(request_);
  }
  expectedFlow_ = 0;
  pending_ = awaiting_ = false;
  flow_.clear();
  candidate_.clear();
}
void Series::leaveSeries() {
  if (live_) closeSeriesFlow();
  live_ = loading_ = pending_ = signInNeeded_ = false;
  members_.clear();
  plan_.clear();
  flow_.clear();
  request_ = {};
  query_.offset = 0;
  query_.seriesKey.fill(0);
  count_.fill(0);
  scene_ = SeriesScene::Page;
}
void Series::requestSeriesPage() {
  members_.clear();
  plan_.clear();
  loading_ = true;
  query_.requestId = ++counter_;
  request_.series.count = 3;
  request_.series.startKnown = false;
  request_.series.requestId = ++counter_;
  pageRequestId_ = request_.series.requestId;
  const bool rows = library_.requestQuery(query_);
  const bool seriesSummaryAccepted = library_.requestSeries(request_.series);
  if (!rows) {
    members_.availability = Availability::Unavailable;
    loading_ = false;
  }
  if (!seriesSummaryAccepted) plan_.availability = Availability::Unsupported;
}
void Series::adoptSeriesFlow() {
  if (!candidate_.flow || !seriesOwner(candidate_.owner, owner_) || candidate_.requestId != request_.series.requestId ||
      (expectedFlow_ && candidate_.flow != expectedFlow_) || candidate_.plan.count > SeriesNextMax ||
      candidate_.queue.count > SeriesNextMax || candidate_.fileCount > SeriesFileChoices ||
      candidate_.fileRow >= SeriesNextMax)
    return;
  if (candidate_.revision < flow_.revision || (awaiting_ && expectedFlow_ && candidate_.revision <= awaitRevision_))
    return;
  flow_ = candidate_;
  awaiting_ = false;
  expectedFlow_ = candidate_.flow;
  request_.flow = expectedFlow_;
  request_.token = candidate_.plan.token;
  request_.review = candidate_.reviewId;
  pending_ = candidate_.pending;
  switch (candidate_.scene) {
    case SeriesFlowScene::Preview:
      scene_ = SeriesScene::Preview;
      break;
    case SeriesFlowScene::Files:
      scene_ = SeriesScene::Files;
      break;
    case SeriesFlowScene::Review:
      scene_ = SeriesScene::Review;
      break;
    case SeriesFlowScene::Notice:
      scene_ = SeriesScene::Notice;
      break;
    case SeriesFlowScene::Queue:
      scene_ = SeriesScene::Queue;
      break;
  }
}
bool Series::sendSeries(SeriesAction action) {
  if (!currentSeries() || pending_) return false;
  request_.action = action;
  if (library_.seriesAction(request_) != CommandStatus::Accepted) {
    if (action == SeriesAction::Status) return false;
    flow_.scene = SeriesFlowScene::Notice;
    flow_.notice = SeriesNotice::Unavailable;
    scene_ = SeriesScene::Notice;
    return false;
  }
  awaitRevision_ = flow_.revision;
  awaiting_ = pending_ = true;
  library_.readSeriesFlow(candidate_);
  adoptSeriesFlow();
  return true;
}
bool Series::pollSeries() {
  if (!live_) return false;
  const auto account = library_.accountView();
  const bool signInNeeded = account.available && account.accountSupported && !account.signedIn;
  const bool accountChanged = signInNeeded != signInNeeded_;
  signInNeeded_ = signInNeeded;
  if (!currentSeries()) {
    leaveSeries();
    return true;
  }
  if (scene_ == SeriesScene::Count) return accountChanged;
  if (awaiting_) {
    library_.readSeriesFlow(candidate_);
    adoptSeriesFlow();
    return !awaiting_;
  }
  if (scene_ == SeriesScene::Page) {
    const auto publication = members_.publication;
    const bool wasLoading = loading_;
    library_.readQuery(members_);
    if (members_.requestId != query_.requestId || members_.provenance.generation != generation_ ||
        members_.provenance.accountEpoch != epoch_ || members_.provenance.owner != owner_.owner ||
        members_.count > query_.limit) {
      members_.clear();
      members_.availability = Availability::Unavailable;
    }
    if (plan_.requestId != pageRequestId_ || plan_.pending || loading_) library_.readSeries(plan_);
    if (plan_.requestId != pageRequestId_) plan_.clear();
    loading_ =
        members_.requestId == query_.requestId && members_.availability == Availability::NotSaved && plan_.pending;
    return wasLoading != loading_ || publication != members_.publication;
  }
  if (scene_ == SeriesScene::Queue && flow_.queue.complete) return false;
  const auto previous = flow_.revision;
  const auto scene = scene_;
  if (scene_ == SeriesScene::Queue && !pending_) {
    request_.action = SeriesAction::Status;
    library_.seriesAction(request_);
  }
  library_.readSeriesFlow(candidate_);
  if (!expectedFlow_ || candidate_.flow != expectedFlow_ || !seriesOwner(candidate_.owner, owner_) ||
      candidate_.requestId != request_.series.requestId || candidate_.revision < previous)
    return false;
  adoptSeriesFlow();
  return previous != flow_.revision || scene != scene_ || accountChanged;
}
void Series::nextSeriesPage(int direction) {
  if (!currentSeries() || scene_ != SeriesScene::Page) return;
  if (direction > 0 && members_.hasMore)
    query_.offset += query_.limit;
  else if (direction < 0 && query_.offset)
    query_.offset = query_.offset >= query_.limit ? query_.offset - query_.limit : 0;
  else
    return;
  requestSeriesPage();
}
void Series::openSeriesBook(uint8_t row) {
  if (currentSeries() && scene_ == SeriesScene::Page && row < members_.count)
    library_.showBook(members_.entries[row].book.key);
}
void Series::searchSeries() {
  if (currentSeries() && scene_ == SeriesScene::Page) library_.searchSeries(request_.series.key);
}
void Series::downloadSeries() {
  if (!currentSeries() || scene_ != SeriesScene::Page || !downloadEnabled() ||
      members_.availability != Availability::Ready || !members_.count)
    return;
  closeSeriesFlow();
  request_.series.requestId = ++counter_;
  request_.series.count = 3;
  request_.series.startKnown = false;
  request_.flow = 0;
  request_.token = 0;
  sendSeries(SeriesAction::Begin);
}
void Series::retrySeriesPage() {
  if (!currentSeries() || scene_ != SeriesScene::Page || !retryEnabled()) return;
  library_.submit(Intent::Refresh);
  requestSeriesPage();
}
void Series::reviewSeries() {
  if (scene_ == SeriesScene::Preview && flow_.plan.count)
    sendSeries(flow_.online ? SeriesAction::ReviewFiles : SeriesAction::ConnectAndReview);
}
void Series::changeSeriesCount() {
  if (!currentSeries() || scene_ != SeriesScene::Preview || pending_) return;
  count_.fill(0);
  const auto n = request_.series.count;
  count_[0] = n == 10 ? '1' : char('0' + n);
  if (n == 10) count_[1] = '0';
  scene_ = SeriesScene::Count;
}
bool Series::appendSeriesCount(char seriesDigitByte) {
  if (!currentSeries() || scene_ != SeriesScene::Count || seriesDigitByte < '0' || seriesDigitByte > '9') return false;
  const auto n = seriesTextLength(count_.data());
  if (n + 1 >= 8) return false;
  count_[n] = seriesDigitByte;
  count_[n + 1] = 0;
  return true;
}
void Series::eraseSeriesCount() {
  if (scene_ == SeriesScene::Count) {
    const auto n = seriesTextLength(count_.data());
    if (n) count_[n - 1] = 0;
  }
}
void Series::stepSeriesCount(int direction) {
  if (!currentSeries() || scene_ != SeriesScene::Count) return;
  uint32_t n = 0;
  for (char c : count_) {
    if (!c) break;
    n = n * 10 + static_cast<uint32_t>(c - '0');
  }
  if (n < 1 || n > 10) n = 3;
  if (direction > 0 && n < 10)
    ++n;
  else if (direction < 0 && n > 1)
    --n;
  count_.fill(0);
  count_[0] = n == 10 ? '1' : char('0' + n);
  if (n == 10) count_[1] = '0';
}
void Series::submitSeriesCount() {
  if (!currentSeries() || scene_ != SeriesScene::Count || !count_[0]) return;
  uint32_t n = 0;
  for (char c : count_) {
    if (!c) break;
    n = n * 10 + static_cast<uint32_t>(c - '0');
    if (n > 10) return;
  }
  if (n < 1) return;
  request_.series.count = n;
  sendSeries(SeriesAction::Preview);
}
void Series::chooseSeriesFile(uint8_t index) {
  if (scene_ != SeriesScene::Files || index >= flow_.fileCount || index >= SeriesFileChoices) return;
  request_.choice = flow_.files[index].choice;
  request_.row = flow_.fileRow;
  sendSeries(SeriesAction::ChooseFile);
}
void Series::startSeriesFiles() {
  if (scene_ == SeriesScene::Review) sendSeries(SeriesAction::Start);
}
void Series::earlierSeriesGap() {
  if (scene_ != SeriesScene::Preview || !flow_.plan.earlierKnown) return;
  request_.series.start = flow_.plan.earlier.book.key;
  request_.series.startKnown = true;
  sendSeries(SeriesAction::Preview);
}
void Series::resumeSeriesQueue() {
  if (scene_ == SeriesScene::Queue && flow_.queue.paused)
    sendSeries(flow_.online ? SeriesAction::Resume : SeriesAction::ConnectAndResume);
}
void Series::cancelSeriesRow(uint8_t row) {
  if (scene_ != SeriesScene::Queue || row >= flow_.queue.count) return;
  const auto& item = flow_.queue.items[row];
  if (!item.cancelable || item.state == SeriesItemState::Complete || item.state == SeriesItemState::Canceled) return;
  request_.row = row;
  sendSeries(SeriesAction::CancelRow);
}
void Series::cancelSeriesRemaining() {
  if (scene_ == SeriesScene::Queue && !flow_.queue.complete) sendSeries(SeriesAction::CancelRemaining);
}
void Series::backSeries() {
  if (!currentSeries()) return;
  if (scene_ == SeriesScene::Page) {
    leaveSeries();
    return;
  }
  if (scene_ == SeriesScene::Review) {
    sendSeries(SeriesAction::Preview);
    return;
  }
  const bool refresh = scene_ == SeriesScene::Queue;
  closeSeriesFlow();
  scene_ = SeriesScene::Page;
  if (refresh) requestSeriesPage();
}
}  // namespace booksync
