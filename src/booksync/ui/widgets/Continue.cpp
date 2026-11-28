// SPDX-License-Identifier: GPL-3.0-or-later
#include "booksync/ui/widgets/Continue.h"
namespace booksync::widgets {
WindowRequest Continue::request() { return {0, 1, LibraryBytes, WindowSource::Continue}; }
WidgetView Continue::view(WidgetView viewValue) { return viewValue; }
void Continue::paint(Canvas& canvas, WidgetView viewValue, const ContinueValues& values, WidgetBox box, bool focused) {
  if (box.width < 180 || box.height < 180 || box.line <= 0 || box.line > 26) return;
  const bool black = false;
  const auto state = values.supplied                             ? values.state
                     : viewValue.viewState == WidgetState::Known ? ContinueState::Ready
                                                                 : ContinueState::NotSaved;
  canvas.fill({0, 0, box.width, box.height}, true, 9);
  const int microLine = box.line * 3 / 4;
  const bool fullContext = box.width >= 360 && box.height >= 210;
  const int titleHeight = box.line * (fullContext && box.titleLines > 1 ? 2 : 1);
  const int centeredShift = fullContext ? (titleHeight - box.line) / 2 : 0;
  const int readingY = fullContext ? 26 - centeredShift : 12;
  const int titleTop = fullContext ? 44 - centeredShift : 16 + microLine;
  const int coverWidth = fullContext ? 116 : 72;
  const int textX = coverWidth + 28;
  const int width = box.width - textX - 12;
  const bool readable =
      viewValue.viewState == WidgetState::Known && state == ContinueState::Ready && viewValue.rows && viewValue.count;
  canvas.text({readable ? textX : 12, readable ? readingY : 12,
               !readable                                          ? box.width - 24
               : fullContext && (values.stale || viewValue.stale) ? width - 150
                                                                  : width,
               readable && fullContext ? 14 : microLine},
              readable ? Label::Reading : Label::Continue, black, true);
  if (viewValue.viewState != WidgetState::Known || state != ContinueState::Ready || !viewValue.rows ||
      !viewValue.count) {
    Label label = Label::NotSaved;
    if (state == ContinueState::Empty || viewValue.viewState == WidgetState::Empty) label = Label::EmptyContinue;
    if (state == ContinueState::Off) label = Label::Off;
    if (state == ContinueState::Unsupported || viewValue.viewState == WidgetState::Unsupported)
      label = Label::Unsupported;
    const bool error = state == ContinueState::Error || state == ContinueState::Oversize ||
                       viewValue.viewState == WidgetState::Error || viewValue.viewState == WidgetState::Oversize;
    if (error) label = Label::Error;
    if (state == ContinueState::Oversize || viewValue.viewState == WidgetState::Oversize) label = Label::OversizedData;
    canvas.text({12, 48, box.width - 24, box.line * 2}, label, black);
    if (error) canvas.text({12, box.height - 58, box.width - 24, 50}, Label::Retry, black, true);
    return;
  }
  const auto& book = viewValue.rows[0];
  const int top = titleTop;
  const int coverHeight = coverWidth * 3 / 2;
  canvas.cover({12, (box.height - coverHeight) / 2, coverWidth, coverHeight}, book, black);
  canvas.title({textX, top, width, titleHeight}, book.title.data(), black, true);
  if (fullContext && values.seriesLabel[0])
    canvas.title({textX, top + titleHeight + 6, width, microLine}, values.seriesLabel.data(), black, false, 192);
  if (fullContext && values.chapterLabel[0])
    canvas.title({textX, top + titleHeight + 26, width, microLine}, values.chapterLabel.data(), black, false, 192);
  const int progressY = top + titleHeight + (fullContext ? 53 : 6);
  const bool positionKnown = values.supplied ? values.progressKnown : book.readingKnown && book.pages;
  const uint32_t percent =
      values.supplied ? values.progressPercent
      : book.pages ? static_cast<uint32_t>(uint64_t(book.page < book.pages ? book.page : book.pages) * 100 / book.pages)
                   : 0;
  if (positionKnown) {
    canvas.text({box.width - 64, progressY - 2, 52, box.line}, Label::Progress, black, true, percent, 100);
    const int barY = progressY + 3;
    const int barWidth = width - 52;
    canvas.border({textX, barY, barWidth, 7}, black);
    canvas.fill({textX, barY, int(uint64_t(barWidth) * percent / 100), 7}, black);
  }
  const int desiredActionY = fullContext ? top + titleHeight + 78 : box.height - 58;
  const int actionY = desiredActionY < box.height - 58 ? desiredActionY : box.height - 58;
  const int actionWidth = fullContext ? 108 : 86;
  if (focused && !box.touch) canvas.border({textX - 4, actionY - 4, actionWidth + 8, 58}, false, 2, 27);
  canvas.fill({textX, actionY, actionWidth, 50}, false, 24);
  canvas.border({textX, actionY, actionWidth, 50}, black, 1, 24);
  canvas.text({textX + 8, actionY + 14, actionWidth - 16, microLine}, Label::Resume, true, true);
  const int metaX = textX + actionWidth + 12;
  if (values.timeLeftKnown && box.width - metaX >= 120)
    canvas.text({metaX, actionY + 10, box.width - metaX - 8, 15}, Label::TimeLeft, black, false, values.timeLeftAmount,
                static_cast<uint8_t>(values.timeLeftUnit));
  if (values.finishDayKnown && box.width - metaX >= 120)
    canvas.text({metaX, actionY + 28, box.width - metaX - 8, 14}, Label::FinishDayText, black, false,
                values.finishWeekday);
  if (values.stale || viewValue.stale)
    canvas.text({fullContext ? box.width - 150 : textX, fullContext ? readingY : progressY + box.line + 8,
                 fullContext ? 138 : width, fullContext ? 14 : microLine},
                fullContext && values.ageKnown ? Label::SavedAge : Label::SavedUnknown, black, false, values.ageDays);
}
}  // namespace booksync::widgets
