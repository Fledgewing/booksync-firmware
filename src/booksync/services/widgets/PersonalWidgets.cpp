// SPDX-License-Identifier: GPL-3.0-or-later
#include "PersonalWidgets.h"

#include <Logging.h>

#include <algorithm>
namespace booksync {
namespace {
constexpr uint32_t HeaderBytes = sizeof(PersonalWidgetValues) - sizeof(PersonalWidgetValues::books);
PersonalWidgetState personalWidgetState(PersonalState state) {
  switch (state) {
    case PersonalState::Ready:
      return PersonalWidgetState::Ready;
    case PersonalState::Empty:
      return PersonalWidgetState::Empty;
    case PersonalState::Off:
      return PersonalWidgetState::Off;
    case PersonalState::Unsupported:
      return PersonalWidgetState::Unsupported;
    case PersonalState::Unavailable:
      return PersonalWidgetState::Error;
    case PersonalState::Oversized:
      return PersonalWidgetState::Oversize;
    default:
      return PersonalWidgetState::NotSaved;
  }
}
bool scopedBook(const BookRow& book, ReadingScope scope) {
  return !book.key.provenance.owner ||
         (book.key.provenance.owner == scope.owner && book.key.provenance.accountEpoch == scope.epoch);
}
bool allowanceFits(PersonalWidgetAllowance allowance, uint8_t required) {
  return allowance.rows >= required && allowance.rows <= LibraryRows && allowance.bytes <= LibraryBytes &&
         allowance.bytes >= HeaderBytes + required * sizeof(BookRow);
}
void stampPersonal(PersonalWidgetValues& out, PersonalWidgetCapture capture, uint8_t count, uint32_t total) {
  out.count = out.rows = count;
  out.total = total;
  out.bytes = HeaderBytes + count * sizeof(BookRow);
  out.stale = capture.stale;
  out.ageKnown = capture.stale && capture.ageKnown;
  out.ageDays = out.ageKnown ? capture.ageDays : 0;
}
}  // namespace
bool projectRecommendedWidget(const PersonalPick& pick, PersonalWidgetCapture capture,
                              PersonalWidgetAllowance allowance, PersonalWidgetValues& out) {
  out.clear();
  if (!allowanceFits(allowance, 1)) {
    LOG_ERR("BSWP", "Recommended needs one materialized book row");
    out.state = PersonalWidgetState::Oversize;
    return false;
  }
  out.state = personalWidgetState(pick.state);
  if (pick.state > PersonalState::Oversized) out.state = PersonalWidgetState::Error;
  if (out.state != PersonalWidgetState::Ready) {
    if (out.state == PersonalWidgetState::Error || out.state == PersonalWidgetState::Oversize) {
      LOG_ERR("BSWP", "Recommended source is unavailable or oversized");
      return false;
    }
    return true;
  }
  if (!scopedBook(pick.record.book, capture.scope)) {
    out.clear();
    return true;
  }
  if (!pick.count || !pick.record.book.title[0] || pick.record.book.title.back()) {
    LOG_ERR("BSWP", "Recommended pick is incomplete");
    out.clear();
    out.state = PersonalWidgetState::Error;
    return false;
  }
  out.books[0] = pick.record.book;
  stampPersonal(out, capture, 1, pick.count);
  return true;
}
bool DiscoverProjection::fail(PersonalWidgetState state, const char* reason) {
  LOG_ERR("BSWD", "%s", reason);
  out_.clear();
  out_.state = state;
  busy_ = false;
  return false;
}
bool DiscoverProjection::begin(int64_t day, int64_t pick, PersonalWidgetCapture capture,
                               PersonalWidgetAllowance allowance) {
  out_.clear();
  busy_ = false;
  selected_ = 0;
  wanted_ = 4;
  total_ = 0;
  if (!allowanceFits(allowance, 4))
    return fail(PersonalWidgetState::Oversize, "Discover requires an explicit four-row allowance");
  capture_ = capture;
  day_ = day;
  pick_ = std::clamp<int64_t>(pick, 0, 1000000000);
  if (!personal_.begin(PersonalKind::Discover, day_, pick_, capture.scope))
    return fail(PersonalWidgetState::Error, "Discover F39 start failed");
  busy_ = true;
  return true;
}
bool DiscoverProjection::tick(PersonalWidgetCapture current, uint16_t allowance) {
  if (!allowance || allowance > PersonalReading::Batch) return false;
  if (!(current == capture_)) {
    out_.clear();
    busy_ = false;
    return true;
  }
  if (!busy_) return true;
  if (!personal_.tick(allowance))
    return fail(personalWidgetState(personal_.result().state), "Discover F39 selection failed");
  if (personal_.busy()) return true;
  const auto& pick = personal_.result();
  if (pick.state != PersonalState::Ready) {
    if (selected_) return fail(PersonalWidgetState::Error, "Discover pool retired between selections");
    out_.state = personalWidgetState(pick.state);
    busy_ = false;
    return true;
  }
  if (!selected_) {
    total_ = pick.count;
    wanted_ = std::min<uint32_t>(total_, 4);
  }
  if (!wanted_ || pick.count != total_)
    return fail(PersonalWidgetState::Error, "Discover pool changed between selections");
  const auto& book = pick.record.book;
  if (!scopedBook(book, capture_.scope)) {
    out_.clear();
    busy_ = false;
    return true;
  }
  if (!book.title[0] || book.title.back()) return fail(PersonalWidgetState::Error, "Discover book title invalid");
  for (uint8_t i = 0; i < selected_; ++i)
    if (out_.books[i].key == book.key) return fail(PersonalWidgetState::Error, "Discover repeated a book identity");
  out_.books[selected_++] = book;
  if (selected_ == wanted_) {
    stampPersonal(out_, capture_, selected_, total_);
    out_.state = PersonalWidgetState::Ready;
    busy_ = false;
    return true;
  }
  // Compute modulo before adding the ordinal, avoiding the F39 public pick clamp
  // at one billion and preserving the companion's exact cyclic ordering.
  const auto nextPick = (pick_ % total_ + selected_) % total_;
  if (!personal_.begin(PersonalKind::Discover, day_, nextPick, capture_.scope))
    return fail(PersonalWidgetState::Error, "Discover next rank failed");
  return true;
}
}  // namespace booksync
