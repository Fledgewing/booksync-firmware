// SPDX-License-Identifier: GPL-3.0-or-later
#include "SeriesFlow.h"

#include <Logging.h>

#include <cstring>
#include <limits>
namespace booksync {
void SeriesFileSet::clear() {
  for (auto& row : files) {
    auto& s = row.selected;
    s.identity = {};
    s.server.fill(0);
    s.account.fill(0);
    s.book.fill(0);
    s.file.fill(0);
    s.variant.fill(0);
    s.version.fill(0);
    s.proof.fill(0);
    s.path.fill(0);
    s.bytes = 0;
    s.sizeKnown = false;
    row.label.fill(0);
    row.destination.fill(0);
  }
  count = 0;
  complete = false;
}
SeriesFlow::SeriesFlow(SeriesPlan& planner, SeriesQueue& queue, SeriesFlowPorts ports, DeliveryTicket owner)
    : planner_(planner), queue_(queue), ports_(ports), owner_(owner) {
  valid_ = owner.owner && owner.epoch && ports.online && ports.files && ports.selectionCurrent && ports.connect &&
           ports.connection && ports.permitted;
}
bool SeriesFlow::sameOwner(DeliveryTicket owner) const {
  return owner.owner == owner_.owner && owner.epoch == owner_.epoch;
}
void SeriesFlow::change(SeriesFlowScene scene, bool pending) {
  view_.scene = scene;
  view_.pending = pending;
  view_.online = ports_.online(ports_.context);
  ++view_.revision;
}
void SeriesFlow::notice(SeriesNotice reason) {
  phase_ = Phase::Idle;
  view_.notice = reason;
  change(SeriesFlowScene::Notice);
}
bool SeriesFlow::beginSeriesPreview(const SeriesRequest& request) {
  if (!request.count || request.count > SeriesNextMax || !request.key[0] ||
      !std::memchr(request.key.data(), 0, request.key.size()))
    return false;
  request_.series = request;
  reviewToken_ = 0;
  review_.clear();
  options_.clear();
  view_.plan.clear();
  if (!planner_.request(request)) {
    notice(SeriesNotice::PreviewError);
    return true;
  }
  phase_ = Phase::Preview;
  change(SeriesFlowScene::Preview, true);
  return true;
}
void SeriesFlow::resolve() {
  if (!view_.plan.count || !view_.plan.token) {
    notice(SeriesNotice::NoEligible);
    return;
  }
  review_.clear();
  review_.token = view_.plan.token;
  review_.request = request_.series;
  review_.count = view_.plan.count;
  if (reviewCounter_ == std::numeric_limits<uint64_t>::max()) {
    notice(SeriesNotice::Unavailable);
    return;
  }
  view_.reviewId = ++reviewCounter_;
  for (auto& row : view_.review) {
    row.label.fill(0);
    row.destination.fill(0);
    row.bytes = 0;
    row.sizeKnown = row.supported = false;
  }
  reviewToken_ = view_.plan.token;
  next_ = 0;
  options_.clear();
  phase_ = Phase::Files;
  change(SeriesFlowScene::Preview, true);
}
void SeriesFlow::selectSeriesOption(uint8_t option) {
  if (next_ >= review_.count || option >= options_.count) return;
  const auto& chosen = options_.files[option];
  auto& row = review_.books[next_];
  row.file = chosen.selected;
  row.key = view_.plan.next[next_].book.key;
  row.title = view_.plan.next[next_].book.title;
  row.variantLabel.fill(0);
  std::memcpy(row.variantLabel.data(), chosen.label.data(), row.variantLabel.size() - 1);
  row.destination = chosen.destination;
  row.state = SeriesItemState::Queued;
  auto& shown = view_.review[next_];
  shown.label = chosen.label;
  shown.destination = chosen.destination;
  shown.bytes = chosen.selected.bytes;
  shown.sizeKnown = chosen.selected.sizeKnown;
  shown.supported = true;
  ++next_;
  options_.clear();
  phase_ = Phase::Files;
  change(SeriesFlowScene::Files, true);
}
void SeriesFlow::pollQueue() {
  if (!queue_.read(view_.queue.id, view_.queue)) {
    if (view_.queue.availability == Availability::Unavailable) notice(SeriesNotice::Unavailable);
    return;
  }  // Another file may own SD.
  phase_ = Phase::Idle;
  change(SeriesFlowScene::Queue);
}
bool SeriesFlow::action(const SeriesFlowRequest& input) {
  if (!valid_ || !sameOwner(input.owner)) return false;
  if (input.action == SeriesAction::Begin) {
    if (!input.series.key[0] || !std::memchr(input.series.key.data(), 0, input.series.key.size()) ||
        !input.series.count || input.series.count > SeriesNextMax)
      return false;
    finding_ = false;
    if (counter_ == std::numeric_limits<uint64_t>::max()) return false;
    planner_.cancel();
    view_.clear();
    request_ = input;
    request_.flow = ++counter_;
    view_.flow = request_.flow;
    view_.owner = owner_;
    view_.requestId = input.series.requestId;
    live_ = true;
    const auto existing = queue_.find(input.series.key.data());
    if (existing) {
      view_.queue.id = existing;
      if (queue_.read(existing, view_.queue) && !view_.queue.complete) {
        change(SeriesFlowScene::Queue);
        return true;
      }
      // Defer an existing queue read rather than showing a false preview while
      // an unrelated transfer owns SD. A completed queue re-enters preview.
      if (!view_.queue.complete) {
        finding_ = true;
        phase_ = Phase::Queue;
        change(SeriesFlowScene::Queue, true);
        return true;
      }
    }
    return beginSeriesPreview(input.series);
  }
  if (!live_ || input.series.requestId != view_.requestId || (input.flow && input.flow != view_.flow)) return false;
  if (input.action == SeriesAction::Close) {
    planner_.cancel();
    live_ = false;
    phase_ = Phase::Idle;
    view_.clear();
    review_.clear();
    options_.clear();
    return true;
  }
  if (view_.pending) return false;
  switch (input.action) {
    case SeriesAction::Preview:
      if (input.series.key != request_.series.key) return false;
      return beginSeriesPreview(input.series);
    case SeriesAction::ReviewFiles:
    case SeriesAction::ConnectAndReview:
      if (view_.scene != SeriesFlowScene::Preview || input.token != view_.plan.token) return false;
      if (input.action == SeriesAction::ConnectAndReview) {
        if (!ports_.connect(ports_.context)) {
          notice(SeriesNotice::ConnectFailed);
          return true;
        }
        phase_ = Phase::ConnectReview;
        change(SeriesFlowScene::Preview, true);
      } else if (ports_.online(ports_.context))
        resolve();
      else
        notice(SeriesNotice::ConnectFailed);
      return true;
    case SeriesAction::ChooseFile:
      if (view_.scene != SeriesFlowScene::Files || input.row != next_ || input.token != reviewToken_ ||
          input.review != view_.reviewId)
        return false;
      for (uint8_t i = 0; i < options_.count; ++i)
        if (input.choice == view_.files[i].choice) {
          selectSeriesOption(i);
          return true;
        }
      return false;
    case SeriesAction::Start:
      if (view_.scene != SeriesFlowScene::Review || input.token != reviewToken_ || input.review != view_.reviewId ||
          !reviewToken_)
        return false;
      if (!ports_.online(ports_.context)) {
        notice(SeriesNotice::Unavailable);
        return true;
      }
      if (!planner_.request(request_.series)) {
        notice(SeriesNotice::Unavailable);
        return true;
      }
      phase_ = Phase::ValidateStart;
      change(SeriesFlowScene::Review, true);
      return true;
    case SeriesAction::Status:
      if (view_.scene != SeriesFlowScene::Queue) return false;
      phase_ = Phase::Queue;
      change(SeriesFlowScene::Queue, true);
      return true;
    case SeriesAction::Resume:
    case SeriesAction::ConnectAndResume:
      if (view_.scene != SeriesFlowScene::Queue || !view_.queue.paused) return false;
      if (input.action == SeriesAction::ConnectAndResume) {
        if (!ports_.connect(ports_.context)) {
          change(SeriesFlowScene::Queue);
          return true;
        }
        phase_ = Phase::ConnectResume;
        change(SeriesFlowScene::Queue, true);
      } else if (ports_.online(ports_.context) && queue_.resume(view_.queue.id)) {
        phase_ = Phase::Queue;
        change(SeriesFlowScene::Queue, true);
      }
      return true;
    case SeriesAction::CancelRow:
    case SeriesAction::CancelRemaining:
      if (view_.scene != SeriesFlowScene::Queue || view_.queue.complete) return false;
      if (input.action == SeriesAction::CancelRow &&
          (input.row >= view_.queue.count || !view_.queue.items[input.row].job ||
           view_.queue.items[input.row].state == SeriesItemState::Complete ||
           view_.queue.items[input.row].state == SeriesItemState::Canceled))
        return false;
      if (!queue_.cancel(view_.queue.id, input.action == SeriesAction::CancelRow ? input.row : SeriesNextMax)) {
        notice(SeriesNotice::Unavailable);
        return true;
      }
      phase_ = Phase::Queue;
      change(SeriesFlowScene::Queue, true);
      return true;
    default:
      return false;
  }
}
void SeriesFlow::tick(DeliveryTicket current) {
  queue_.tick(current);
  if (!live_) return;
  if (!sameOwner(current)) {
    planner_.cancel();
    view_.clear();
    review_.clear();
    options_.clear();
    live_ = false;
    valid_ = false;
    phase_ = Phase::Idle;
    return;
  }
  if (!ports_.permitted(ports_.context)) return;
  switch (phase_) {
    case Phase::Preview:
    case Phase::ValidateStart: {
      planner_.tick();
      if (planner_.busy()) return;
      planner_.copyPlan(view_.plan);
      if (view_.plan.availability != Availability::Ready || view_.plan.provenance.owner != owner_.owner ||
          view_.plan.provenance.accountEpoch != owner_.epoch) {
        notice(view_.plan.membershipIncomplete ? SeriesNotice::Incomplete : SeriesNotice::PreviewError);
        return;
      }
      if (phase_ == Phase::Preview) {
        if (!view_.plan.count && !view_.plan.earlierKnown) {
          notice(SeriesNotice::NoEligible);
          return;
        }
        phase_ = Phase::Idle;
        change(SeriesFlowScene::Preview);
        return;
      }
      if (view_.plan.token != reviewToken_ || view_.plan.count != review_.count) {
        notice(SeriesNotice::Unavailable);
        return;
      }
      for (uint8_t i = 0; i < review_.count; ++i) {
        const auto& row = review_.books[i];
        if (!(row.key == view_.plan.next[i].book.key) ||
            (row.state != SeriesItemState::Unsupported &&
             !ports_.selectionCurrent(ports_.context, row.key, row.file))) {
          notice(SeriesNotice::Unavailable);
          return;
        }
      }
      phase_ = Phase::Start;
      return;
    }
    case Phase::Files:
      if (next_ == review_.count) {
        phase_ = Phase::Idle;
        change(SeriesFlowScene::Review);
        return;
      }
      options_.clear();
      view_.fileRow=next_;
      if (!ports_.files(ports_.context, view_.plan.next[next_].book.key, options_) || !options_.complete ||
          options_.count > SeriesFileChoices) {
        notice(SeriesNotice::FileUnavailable);
        return;
      }
      if (!options_.count) {
        auto& row = review_.books[next_];
        row.key = view_.plan.next[next_].book.key;
        row.title = view_.plan.next[next_].book.title;
        row.state = SeriesItemState::Unsupported;
        view_.review[next_].supported = false;
        ++next_;
        return;
      }
      if (choiceCounter_ > std::numeric_limits<uint64_t>::max() - options_.count) {
        notice(SeriesNotice::FileUnavailable);
        return;
      }
      for (uint8_t i = 0; i < options_.count; ++i) {
        if (!std::memchr(options_.files[i].label.data(), 0, options_.files[i].label.size()) ||
            !std::memchr(options_.files[i].destination.data(), 0, options_.files[i].destination.size())) {
          notice(SeriesNotice::FileUnavailable);
          return;
        }
        view_.files[i].label = options_.files[i].label;
        view_.files[i].bytes = options_.files[i].selected.bytes;
        view_.files[i].sizeKnown = options_.files[i].selected.sizeKnown;
        view_.files[i].choice = ++choiceCounter_;
      }
      if (options_.count == 1) {
        selectSeriesOption(0);
        return;
      }
      view_.fileCount = options_.count;
      view_.fileRow = next_;
      phase_ = Phase::Idle;
      change(SeriesFlowScene::Files);
      return;
    case Phase::Start: {
      if (queue_.transferBusy()) return;
      const auto id = queue_.start(review_);
      if (!id) {
        notice(SeriesNotice::Unavailable);
        return;
      }  // Existing file yields first.
      view_.queue.id = id;
      phase_ = Phase::Queue;
      change(SeriesFlowScene::Queue, true);
      return;
    }
    case Phase::Queue: {
      pollQueue();
      if (finding_ && !view_.pending) {
        finding_ = false;
        if (view_.queue.complete) beginSeriesPreview(request_.series);
      }
      return;
    }
    case Phase::ConnectReview:
    case Phase::ConnectResume: {
      const auto result = ports_.connection(ports_.context);
      if (result == SeriesConnect::Pending) return;
      const bool resuming = phase_ == Phase::ConnectResume;
      if (result != SeriesConnect::Ready || !ports_.online(ports_.context)) {
        if (resuming) {
          phase_ = Phase::Idle;
          change(SeriesFlowScene::Queue);
        } else
          notice(SeriesNotice::ConnectFailed);
        return;
      }
      if (resuming) {
        queue_.resume(view_.queue.id);
        phase_ = Phase::Queue;
        change(SeriesFlowScene::Queue, true);
      } else
        resolve();
      return;
    }
    default:
      return;
  }
}
void SeriesFlow::copyFlow(SeriesFlowView& out) const {
  if (live_)
    out = view_;
  else
    out.clear();
}
}  // namespace booksync

extern "C" bool booksync_series_flow_link(booksync::SeriesFlow& flow, const booksync::SeriesFlowRequest& request,
                                          booksync::DeliveryTicket owner, booksync::SeriesFlowView& out) {
  const bool accepted = flow.action(request);
  flow.tick(owner);
  flow.copyFlow(out);
  return accepted;
}
