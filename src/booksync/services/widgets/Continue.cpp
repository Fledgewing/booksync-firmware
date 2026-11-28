// SPDX-License-Identifier: GPL-3.0-or-later
#include "Continue.h"

#include <Logging.h>
namespace booksync {
void projectContinue(bool available, PersonalState state, const PersonalCard& card, bool stale, uint32_t ageDays,
                     ContinueValues& out, const ContinueContext* context, bool ageKnown,
                     const BookRow* nativePosition) {
  out = {};
  out.supplied = true;
  switch (state) {
    case PersonalState::Ready:
      out.state = available ? ContinueState::Ready : ContinueState::Empty;
      break;
    case PersonalState::Empty:
      out.state = ContinueState::Empty;
      break;
    case PersonalState::Off:
      out.state = ContinueState::Off;
      break;
    case PersonalState::Unsupported:
      out.state = ContinueState::Unsupported;
      break;
    case PersonalState::Unavailable:
      out.state = ContinueState::Error;
      break;
    case PersonalState::Oversized:
      out.state = ContinueState::Oversize;
      break;
    default:
      break;
  }
  if (out.state != ContinueState::Ready) return;
  const uint64_t amount = card.timeLeft.value / (card.timeLeft.value >= 3600 ? 3600 : 60);
  if (card.timeLeft.known && amount > UINT32_MAX) {
    LOG_ERR("BSW", "Continue estimate exceeds bounded display amount");
    out.state = ContinueState::Oversize;
    return;
  }
  if (context) {
    out.seriesLabel = context->seriesLabel;
    out.chapterLabel = context->chapterLabel;
    out.seriesLabel.back() = 0;
    out.chapterLabel.back() = 0;
  }
  out.stale = stale;
  out.ageKnown = stale && ageKnown;
  out.ageDays = out.ageKnown ? ageDays : 0;
  out.timeLeftKnown = card.timeLeft.known;
  out.timeLeftAmount = card.timeLeft.known ? static_cast<uint32_t>(amount) : 0;
  out.timeLeftUnit =
      card.timeLeft.known && card.timeLeft.value >= 3600 ? ContinueTimeUnit::Hours : ContinueTimeUnit::Minutes;
  // Civil years through 9999; hostile epochs must not become plausible weekdays.
  out.finishDayKnown = card.finishDay.known && card.finishDay.value <= 2932896;
  if (card.finishDay.known && !out.finishDayKnown) LOG_ERR("BSW", "Continue forecast day outside supported calendar");
  out.finishWeekday = out.finishDayKnown ? static_cast<uint8_t>(card.finishDay.value % 7) : 0;
  out.progressKnown = card.progress.known;
  out.progressPercent =
      card.progress.known ? static_cast<uint8_t>(card.progress.value < 100 ? card.progress.value : 100) : 0;
  if (!out.progressKnown && nativePosition) projectContinuePosition(*nativePosition, out);
}
void projectContinuePosition(const BookRow& book, ContinueValues& out) {
  out.progressKnown = out.state == ContinueState::Ready && book.readingKnown && book.pages;
  out.progressPercent =
      out.progressKnown
          ? static_cast<uint8_t>(uint64_t(book.page < book.pages ? book.page : book.pages) * 100 / book.pages)
          : 0;
}
}  // namespace booksync
