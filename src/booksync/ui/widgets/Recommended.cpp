// SPDX-License-Identifier: GPL-3.0-or-later
#include "booksync/ui/widgets/Recommended.h"
namespace booksync::widgets {
WindowRequest Recommended::request() {
  return {0, 1, LibraryBytes, WindowSource::Recommended, WindowPresentation::Preview};
}
void Recommended::paint(Canvas& canvas, WidgetView snapshot, WidgetBox box, bool focused) {
  if (box.width < 180 || box.height < 100 || box.line <= 0 || box.line > 26) return;
  canvas.border({0, 0, box.width, box.height}, true, 1, 9);
  (void)focused;
  canvas.text({12, 7, box.width - 48, 20}, Label::RecommendedTitle, true, true);
  canvas.star({box.width - 31, 8, 21, 21});
  const bool hasBook = snapshot.viewState == WidgetState::Known && snapshot.rows && snapshot.count;
  if (!hasBook) {
    Label label = Label::NotSaved;
    if (snapshot.viewState == WidgetState::Empty) label = Label::EmptyRecommended;
    if (snapshot.viewState == WidgetState::Off) label = Label::Off;
    if (snapshot.viewState == WidgetState::Unsupported) label = Label::Unsupported;
    const bool failed = snapshot.viewState == WidgetState::Error || snapshot.viewState == WidgetState::Oversize;
    if (failed) label = snapshot.viewState == WidgetState::Oversize ? Label::OversizedData : Label::Error;
    canvas.text({12, 32, box.width - 24, failed ? box.height - 72 : 64}, label, true);
    if (failed) canvas.text({12, box.height - 40, box.width - 24, 36}, Label::Retry, true, true);
    return;
  }
  canvas.cover({12, box.height - 72, 42, 64}, snapshot.rows[0], true);
  canvas.serifTitle({64, 40, box.width - 76, 20}, snapshot.rows[0].title.data(), true, 192);
  canvas.microText({64, 64, box.width - 76, snapshot.stale ? 20 : 32}, Label::ReadingHistory, true);
  if (snapshot.stale)
    canvas.microText({64, box.height - 22, box.width - 76, 18},
                     snapshot.ageKnown ? Label::SavedAge : Label::SavedUnknown, true, snapshot.ageDays);
}
}  // namespace booksync::widgets
