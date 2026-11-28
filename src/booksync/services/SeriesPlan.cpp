// SPDX-License-Identifier: GPL-3.0-or-later
#include "SeriesPlan.h"

#include <Logging.h>

#include <cmath>
#include <cstring>
namespace booksync {
namespace {
constexpr uint64_t Seed = 14695981039346656037ull;
constexpr QueryFields Required = queryField(QueryField::Title) | queryField(QueryField::Author) |
                                 queryField(QueryField::Series) | queryField(QueryField::SeriesIdentity) |
                                 queryField(QueryField::SeriesPosition) | queryField(QueryField::Status) |
                                 queryField(QueryField::Device) | queryField(QueryField::Last);
template <size_t N>
bool seriesTextValid(const std::array<char, N>& text) {
  return std::memchr(text.data(), 0, N) != nullptr;
}
void hashBytes(uint64_t& hash, const void* bytes, size_t count) {
  const auto* p = static_cast<const uint8_t*>(bytes);
  while (count--) hash = (hash ^ *p++) * 1099511628211ull;
}
template <typename T>
void hashValue(uint64_t& hash, const T& value) {
  hashBytes(hash, &value, sizeof(value));
}
}  // namespace
bool SeriesPlan::unchanged() const {
  const auto now = source_.state(source_.context);
  return now.ready && now.provenance == state_.provenance && now.count == state_.count && now.fields == state_.fields &&
         jobs_.revision(jobs_.context) == jobsRevision_;
}
int SeriesPlan::compare(const SeriesMember& a, const SeriesMember& b) {
  if (a.positionKnown != b.positionKnown) return a.positionKnown ? -1 : 1;
  if (a.positionKnown && a.position != b.position) return a.position < b.position ? -1 : 1;
  if (a.rank != b.rank) return a.rank < b.rank ? -1 : 1;
  return a.book.key.ordinal < b.book.key.ordinal ? -1 : a.book.key.ordinal > b.book.key.ordinal ? 1 : 0;
}
void SeriesPlan::finish(Availability availability) {
  busy_ = false;
  if (availability != Availability::Ready) {
    LOG_ERR("BSS", "Series projection unavailable (%u)", unsigned(availability));
    view_.clear();
  }
  view_.requestId = request_.requestId;
  view_.requested = request_.count;
  view_.provenance = state_.provenance;
  view_.availability = availability;
  view_.pending = false;
}
void SeriesPlan::cancel() { finish(Availability::Unavailable); }
bool SeriesPlan::request(const SeriesRequest& request) {
  if (!source_.state || !source_.read || !jobs_.revision || !jobs_.active || !request.key[0] ||
      !seriesTextValid(request.key) || request.count < 1 || request.count > SeriesNextMax) {
    LOG_ERR("BSS", "Invalid bounded Series request");
    return false;
  }
  request_ = request;
  state_ = source_.state(source_.context);
  jobsRevision_ = jobs_.revision(jobs_.context);
  view_.clear();
  view_.requestId = request.requestId;
  view_.requested = request.count;
  view_.provenance = state_.provenance;
  ordinal_ = lastWork_ = 0;
  selecting_ = currentFound_ = startFound_ = readingFound_ = finishedFound_ = anchorKnown_ = false;
  sawRemaining_ = false;
  allRemainingSaved_ = true;
  anchor_.clear();
  reading_.clear();
  finished_.clear();
  explicit_.clear();
  recent_.fill(0);
  hash_ = Seed;
  hashValue(hash_, state_.provenance.owner);
  hashValue(hash_, state_.provenance.accountEpoch);
  hashValue(hash_, state_.provenance.generation);
  hashValue(hash_, state_.provenance.capture);
  hashValue(hash_, jobsRevision_);
  hashValue(hash_, request.count);
  hashBytes(hash_, request.key.data(), std::strlen(request.key.data()) + 1);
  if (!state_.ready) {
    finish(Availability::NotSaved);
    return true;
  }
  if ((state_.fields & Required) != Required) {
    finish(Availability::Unsupported);
    view_.membershipIncomplete =
        (state_.fields & (queryField(QueryField::Series) | queryField(QueryField::SeriesIdentity))) !=
        (queryField(QueryField::Series) | queryField(QueryField::SeriesIdentity));
    return true;
  }
  if ((request.currentKnown && request.current.provenance != state_.provenance) ||
      (request.startKnown && request.start.provenance != state_.provenance)) {
    finish(Availability::Unavailable);
    return true;
  }
  view_.deviceKnown = true;
  busy_ = view_.pending = true;
  return true;
}
int SeriesPlan::membership() {
  if (record_.seriesCount > 8 || record_.authorCount > 8 || !seriesTextValid(record_.book.title) ||
      !seriesTextValid(record_.last) || unsigned(record_.status) > unsigned(QueryShow::Unknown) ||
      unsigned(record_.onDevice) > unsigned(QueryTruth::Yes))
    return -2;
  int found = -1;
  for (uint8_t i = 0; i < record_.seriesCount; ++i) {
    if (!seriesTextValid(record_.seriesKeys[i]) || !seriesTextValid(record_.series[i]) ||
        (record_.seriesPositionsKnown[i] && !std::isfinite(record_.seriesPositions[i])))
      return -2;
    if (std::strcmp(request_.key.data(), record_.seriesKeys[i].data()) == 0) {
      if (found >= 0) return -2;  // Ambiguous membership cannot silently win.
      found = i;
    }
  }
  for (uint8_t i = 0; i < record_.authorCount; ++i)
    if (!seriesTextValid(record_.authors[i])) return -2;
  return found;
}
void SeriesPlan::project(uint8_t index) {
  candidate_.book = record_.book;
  if (record_.authorCount)
    candidate_.author = record_.authors[0];
  else
    candidate_.author.fill(0);
  candidate_.position = record_.seriesPositions[index];
  candidate_.positionKnown = record_.seriesPositionsKnown[index];
  candidate_.rank = record_.seriesRanks[index];
  candidate_.status = record_.status;
  candidate_.onDevice = record_.onDevice;
}
void SeriesPlan::observe() {
  ++view_.total;
  if (candidate_.status == QueryShow::Finished) ++view_.read;
  const bool reading = candidate_.status == QueryShow::Reading || candidate_.status == QueryShow::Rereading;
  if (reading) ++view_.reading;
  if (candidate_.onDevice == QueryTruth::Unknown) view_.deviceKnown = false;
  if (request_.currentKnown && candidate_.book.key == request_.current) {
    explicit_ = candidate_;
    currentFound_ = true;
  }
  if (request_.startKnown && candidate_.book.key == request_.start) {
    anchor_ = candidate_;
    startFound_ = true;
  }
  if (reading) {
    // Android's ascending "last" comparator places absent dates last. max()
    // therefore preserves that exact unknown-date behavior, with stable-key tie.
    const int recent = bool(record_.last[0]) != bool(recent_[0]) ? (record_.last[0] ? -1 : 1)
                                                                 : std::strcmp(record_.last.data(), recent_.data());
    if (!readingFound_ || recent > 0 || (recent == 0 && candidate_.book.key.ordinal > reading_.book.key.ordinal)) {
      reading_ = candidate_;
      recent_ = record_.last;
      readingFound_ = true;
    }
  }
  if (candidate_.status == QueryShow::Finished && (!finishedFound_ || compare(finished_, candidate_) < 0)) {
    finished_ = candidate_;
    finishedFound_ = true;
  }
  hashValue(hash_, candidate_.book.key.ordinal);
  hashBytes(hash_, candidate_.book.book.data(), candidate_.book.book.size());
  hashBytes(hash_, candidate_.book.edition.data(), candidate_.book.edition.size());
  hashBytes(hash_, candidate_.book.revision.data(), candidate_.book.revision.size());
  hashValue(hash_, candidate_.position);
  hashValue(hash_, candidate_.positionKnown);
  hashValue(hash_, candidate_.rank);
  hashValue(hash_, candidate_.status);
  hashValue(hash_, candidate_.onDevice);
  const auto active = jobs_.active(jobs_.context, candidate_.book.key);
  hashValue(hash_, active);
  hashBytes(hash_, record_.last.data(), std::strlen(record_.last.data()) + 1);
}
void SeriesPlan::select() {
  const int relative = anchorKnown_ ? compare(candidate_, anchor_) : 1;
  const bool after = relative > 0 || (request_.startKnown && relative == 0);
  if (after && candidate_.status != QueryShow::Finished) {
    sawRemaining_ = true;
    allRemainingSaved_ = allRemainingSaved_ && candidate_.onDevice == QueryTruth::Yes;
  }
  if (candidate_.status != QueryShow::Unread || candidate_.onDevice != QueryTruth::No ||
      jobs_.active(jobs_.context, candidate_.book.key) != QueryTruth::No)
    return;
  if (!after) {
    if (!request_.startKnown && relative < 0 && (!view_.earlierKnown || compare(candidate_, view_.earlier) < 0)) {
      view_.earlier = candidate_;
      view_.earlierKnown = true;
    }
    return;
  }
  uint8_t index = 0;
  while (index < view_.count && compare(view_.next[index], candidate_) < 0) ++index;
  if (index >= request_.count) return;
  if (view_.count < request_.count) ++view_.count;
  for (uint8_t i = view_.count - 1; i > index; --i) view_.next[i] = view_.next[i - 1];
  view_.next[index] = candidate_;
}
bool SeriesPlan::tick(uint16_t allowance) {
  lastWork_ = 0;
  if (!busy_) return false;
  if (!unchanged()) {
    finish(Availability::Unavailable);
    return false;
  }
  if (allowance > Batch) allowance = Batch;
  while (busy_ && lastWork_ < allowance) {
    if (ordinal_ == state_.count) {
      if (!selecting_) {
        if (request_.startKnown && !startFound_) {
          finish(Availability::Unavailable);
          break;
        }
        if (!request_.startKnown) {
          if (currentFound_)
            anchor_ = explicit_;
          else if (readingFound_)
            anchor_ = reading_;
          else if (finishedFound_)
            anchor_ = finished_;
        }
        anchorKnown_ = request_.startKnown || currentFound_ || readingFound_ || finishedFound_;
        if (anchorKnown_) hashValue(hash_, anchor_.book.key.ordinal);
        hashValue(hash_, request_.startKnown);
        ordinal_ = 0;
        selecting_ = true;
      } else {
        view_.token = hash_ ? hash_ : 1;
        view_.remainingSaved = sawRemaining_ && allRemainingSaved_;
        finish(Availability::Ready);
      }
      continue;
    }
    const BookKey key{state_.provenance, ordinal_++};
    ++lastWork_;
    if (!source_.read(source_.context, key, record_) || record_.book.key != key) {
      finish(Availability::Unavailable);
      break;
    }
    const int index = membership();
    if (index == -2) {
      finish(Availability::Unavailable);
      break;
    }
    if (index < 0) continue;
    project(uint8_t(index));
    if (selecting_)
      select();
    else {
      if (!view_.title[0]) {
        view_.title = record_.series[index];
        view_.author = candidate_.author;
      }
      observe();
    }
  }
  if (busy_ && !unchanged()) finish(Availability::Unavailable);
  return busy_;
}
void SeriesPlan::copyPlan(SeriesPlanView& out) const {
  if (view_.availability == Availability::Ready && !unchanged()) {
    out.clear();
    out.requestId = request_.requestId;
    out.availability = Availability::Unavailable;
  } else if (busy_) {
    out.clear();
    out.requestId = request_.requestId;
    out.requested = request_.count;
    out.pending = true;
    out.provenance = state_.provenance;
  } else
    out = view_;
}
}  // namespace booksync

extern "C" bool booksync_series_plan_link(booksync::SeriesPlan& worker, const booksync::SeriesRequest& request,
                                          booksync::SeriesPlanView& out) {
  const bool accepted = worker.request(request);
  worker.tick();
  worker.copyPlan(out);
  return accepted;
}
