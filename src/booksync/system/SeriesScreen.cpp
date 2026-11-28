// SPDX-License-Identifier: GPL-3.0-or-later
#include "SeriesScreen.h"

#include <AppCapabilities.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#ifdef SIMULATOR
#include <cstdlib>
#endif

#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "booksync/ui/Series.h"
#include "components/UITheme.h"
#include "fontIds.h"
namespace booksync {
namespace {
#ifdef SIMULATOR
uint32_t paints = 0;
class SeriesActivity;
SeriesActivity* scenarioActivity = nullptr;
struct TextBox {
  Rect bounds{0, 0, 0, 0};
  uint8_t layer = 0;
  char label[96]{};
};
std::array<TextBox, 256> textBoxes_{};
unsigned textBoxCount_ = 0;
uint8_t textLayer_ = 0;
bool geometryOverflow_ = false;
#endif
class SeriesActivity final : public Activity {
  Library& library_;
  Series series_;
  SeriesRequest initial_{};
  uint8_t focus_ = 0, scroll_ = 0;
  uint32_t lastPoll_ = 0;
  bool painted_ = false;
  int textLine_ = 0, smallLine_ = 0, titleLine_ = 0, captionLine_ = 0, memberLine_ = 0;
#if !CROSSINK_APP_CAP_HOME_EDITOR_SETTINGS
  // Three prepared 48x72 covers plus one reusable F25 bitmap. No second framebuffer.
  std::array<std::array<uint8_t, 432>, 3> covers_{};
  std::array<CoverReference, 3> references_{};
  std::array<BookKey, 3> keys_{};
  CoverBitmap bitmap_{};
#endif
  Rect safe() const { return UITheme::getInstance().getScreenSafeArea(renderer, false, false); }
  // Normalize approved design coordinates to the current safe viewport; these
  // reference units are not physical panel dimensions.
  int seriesScale(int value) const {
    return std::max(1, std::min(value * safe().width / 480, value * safe().height / 800));
  }
  int textFont() const { return UI_10_FONT_ID; }
  int memberFont() const { return SERIES_MEMBER_FONT_ID; }
  int titleFont() const { return UI_12_FONT_ID; }
  int seriesTextLine() const { return textLine_; }
  int seriesTop() const {
    const auto s = safe();
    return s.y + seriesScale(80);
  }
  int bottom() const {
    const auto s = safe();
    return s.y + s.height - seriesScale(37);
  }
  void text(Rect r, const char* value, bool bold = false, int font = 0) const {
    if (r.width < 1 || r.height < 1) return;
#ifdef SIMULATOR
    const int selectedFont = font ? font : textFont();
    const auto style = bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const auto ink = renderer.getTextVerticalBounds(selectedFont, value, style);
    if (value[0] && ink.bottom > ink.top) {
      if (textBoxCount_ == textBoxes_.size())
        geometryOverflow_ = true;
      else {
        auto& box = textBoxes_[textBoxCount_++];
        box.bounds = Rect{r.x, r.y, std::min(r.width, renderer.getTextWidth(selectedFont, value, style)),
                          std::max(renderer.getLineHeight(selectedFont), ink.bottom)};
        box.layer = textLayer_;
        std::snprintf(box.label, sizeof(box.label), "%s", value);
        if (ink.top < 0 || ink.bottom > r.height) {
          LOG_ERR("BSS", "Clipped text geometry: %s ink=%d..%d box=%d", value, ink.top, ink.bottom, r.height);
          geometryOverflow_ = true;
        }
      }
    }
#endif
    renderer.beginTextClip(r.x, r.y, r.width, r.height);
    renderer.drawText(font ? font : textFont(), r.x, r.y, value, true,
                      bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    renderer.endTextClip();
  }
  void actionOutline(Rect r, bool focused = false, bool disabled = false) const {
    renderer.drawRoundedRect(r.x, r.y, r.width, r.height, focused && !disabled ? 2 : 1,
                             CROSSINK_APP_CAP_HOME_EDITOR_SETTINGS ? 0 : r.height / 2, true);
    if (disabled) {
      // Stipple the empty frame before drawing its label, using the existing 1-bit buffer.
      const int period = seriesScale(8), ink = seriesScale(4);
      for (int y = r.y; y < r.y + r.height; ++y)
        for (int x = r.x; x < r.x + r.width; ++x)
          if ((x + y) % period >= ink) renderer.drawPixel(x, y, false);
    }
  }
  void buttonText(Rect area, const char* label, bool enabled = true) const {
    const int width = std::min(
        area.width - seriesScale(12),
        renderer.getTextWidth(SERIES_META_FONT_ID, label, enabled ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR));
    text(Rect{area.x + (area.width - width) / 2, area.y + (area.height - smallLine()) / 2, width, smallLine()}, label,
         enabled, SERIES_META_FONT_ID);
  }
  void wrapped(Rect area, const char* value, uint8_t maxLines, bool bold = false, int font = 0) const {
    const int selectedFont = font ? font : textFont();
    const int height = selectedFont == titleFont()              ? titleLine_
                       : selectedFont == SERIES_META_FONT_ID    ? smallLine()
                       : selectedFont == SERIES_CAPTION_FONT_ID ? captionLine_
                                                                : seriesTextLine();
    char chunk[96]{};
    for (uint8_t row = 0; row < maxLines && *value && (row + 1) * height <= area.height; ++row) {
      uint8_t n = 0, lastSpace = 0;
      while (value[n]) {
        const auto lead = static_cast<uint8_t>(value[n]);
        const uint8_t bytes = lead < 0x80 ? 1 : (lead & 0xe0) == 0xc0 ? 2 : (lead & 0xf0) == 0xe0 ? 3 : 4;
        if (n + bytes >= sizeof(chunk)) break;
        uint8_t copied = 0;
        while (copied < bytes && value[n + copied]) {
          chunk[n + copied] = value[n + copied];
          ++copied;
        }
        chunk[n + copied] = 0;
        if (renderer.getTextWidth(selectedFont, chunk, bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR) >
            area.width) {
          chunk[n] = 0;
          break;
        }
        if (value[n] == ' ') lastSpace = n;
        n += copied;
      }
      if (!n) break;
      uint8_t consumed = n;
      if (value[n] && lastSpace) {
        chunk[lastSpace] = 0;
        consumed = lastSpace + 1;
      }
      text(Rect{area.x, area.y + row * height, area.width, height}, chunk, bold, selectedFont);
      value += consumed;
      while (*value == ' ') ++value;
    }
  }
  void outline(Rect r, int thick = 1) const {
    renderer.drawRoundedRect(r.x, r.y, r.width, r.height, thick,
                             CROSSINK_APP_CAP_HOME_EDITOR_SETTINGS ? 0 : seriesScale(10), true);
  }
  int smallLine() const { return smallLine_; }
  int cardTop() const { return totalsBarY(seriesTop() + seriesScale(70)) + seriesScale(19); }
  int cardListTop() const { return cardTop() + seriesTextLine() + smallLine() + seriesScale(60); }
  int cardTitleStep() const {
#ifdef SIMULATOR
    // Regression probe recreates the former fixed-pitch overlap; never in hardware images.
    if (std::getenv("BOOKSYNC_SERIES_GEOMETRY_NEGATIVE")) return seriesScale(18);
#endif
    return smallLine() + seriesScale(3);
  }
  int cardButtonY() const {
    const auto count = std::min<uint8_t>(series_.plan().count, 3);
    return count ? cardListTop() + count * (smallLine() + seriesScale(3)) + seriesScale(8)
                 : cardListTop() + seriesScale(20);
  }
  int membersY() const { return cardButtonY() + seriesScale(50) + seriesScale(22); }
  int memberTextHeight(bool reading) const {
    return memberLine_ + 2 * seriesScale(4) + renderer.getLineHeight(SERIES_CAPTION_FONT_ID) + captionLine_ +
           (reading ? 6 + std::max(3, seriesScale(4)) : 0);
  }
  int memberHeight() const {
    return std::max(CROSSINK_APP_CAP_HOME_EDITOR_SETTINGS ? 72 : 90, memberTextHeight(true) + 4);
  }
  Rect pageRow(uint8_t i) const {
    const auto s = safe();
    return Rect{s.x + seriesScale(16), membersY() + memberLine_ + seriesScale(5) + i * memberHeight(),
                s.width - seriesScale(32), memberHeight()};
  }
  Rect retryButton() const {
    const auto s = safe();
    return Rect{s.x + seriesScale(16), seriesTop() + seriesScale(98), seriesScale(150), seriesScale(50)};
  }
  Rect downloadButton() const {
    const auto s = safe();
    return Rect{s.x + seriesScale(28), cardButtonY(), s.width - seriesScale(56), seriesScale(50)};
  }
  Rect seriesPanel() const {
    const auto s = safe();
    const int available = bottom() - seriesTop() - seriesScale(20);
    int needed = sheetListTop() +
                 (std::min<int>(rowCount(), maxTableRows()) + (series_.scene() == SeriesScene::Review)) * rowHeight() +
                 (series_.scene() == SeriesScene::Review ? 1 : actionCount()) * seriesScale(59) + seriesScale(30);
    if (series_.scene() == SeriesScene::Notice) {
      const int width = s.width - seriesScale(82);
      int textWidth = renderer.getTextWidth(titleFont(), notice(), EpdFontFamily::BOLD);
      if (series_.flow().notice == SeriesNotice::FileUnavailable)
        textWidth +=
            renderer.getTextWidth(titleFont(), series_.flow().plan.next[series_.flow().fileRow].book.title.data(),
                                  EpdFontFamily::BOLD) +
            seriesScale(18);
      const int lines = std::min(8, std::max(1, (textWidth + width - 1) / width));
      needed = lines * renderer.getLineHeight(titleFont()) + seriesScale(100);
    }
    if (series_.scene() == SeriesScene::Count) return Rect{s.x, seriesTop(), s.width, bottom() - seriesTop()};
    const int height = std::min(available, needed);
    const int y = series_.scene() == SeriesScene::Count && CROSSINK_APP_CAP_TOUCH
                      ? bottom() - seriesScale(10) - height
                      : seriesTop() + seriesScale(10) + (available - height) / 2;
    return Rect{s.x + seriesScale(16), y, s.width - seriesScale(32), height};
  }
  Rect keyboard() const {
    const auto s = safe();
    return Rect{s.x + seriesScale(94), bottom() - seriesScale(224), s.width - seriesScale(188), seriesScale(216)};
  }
  Rect keyRect(uint8_t i) const {
    const auto k = keyboard();
    return Rect{k.x + (i % 3) * k.width / 3, k.y + (i / 3) * k.height / 4, k.width / 3 - seriesScale(4),
                k.height / 4 - seriesScale(4)};
  }
  uint8_t rowCount() const {
    switch (series_.scene()) {
      case SeriesScene::Preview:
      case SeriesScene::Review:
        return series_.flow().plan.count;
      case SeriesScene::Files:
        return series_.flow().fileCount;
      case SeriesScene::Queue:
        return series_.flow().queue.count;
      default:
        return 0;
    }
  }
  uint8_t actionCount() const {
    switch (series_.scene()) {
      case SeriesScene::Preview:
        return series_.flow().plan.earlierKnown ? 4 : 3;
      case SeriesScene::Files:
      case SeriesScene::Notice:
        return 1;
      case SeriesScene::Review:
      case SeriesScene::Count:
        return 2;
      case SeriesScene::Queue:
        return 1 + !series_.flow().queue.complete + series_.flow().queue.paused;
      default:
        return 0;
    }
  }
  int rowHeight() const {
    const int content = std::max(50, seriesTextLine() + seriesScale(22));
    return content +
           ((series_.scene() == SeriesScene::Files || series_.scene() == SeriesScene::Queue) ? seriesScale(6) : 0);
  }
  int maxTableRows() const { return std::max(1, seriesScale(326) / rowHeight()); }
  int sheetListTop() const { return series_.scene() == SeriesScene::Queue ? seriesScale(172) : seriesScale(60); }
  int visibleRows() const {
    const auto r = seriesPanel();
    const int end = actionRect(0).y - r.y - seriesScale(6) - (series_.scene() == SeriesScene::Review ? rowHeight() : 0);
    const int available = end - sheetListTop();
    return std::min(maxTableRows(), std::max(1, available / rowHeight()));
  }
  Rect sheetRow(uint8_t i) const {
    const auto r = seriesPanel();
    return Rect{r.x + seriesScale(12), r.y + sheetListTop() + (int(i) - scroll_) * rowHeight(),
                r.width - seriesScale(24), rowHeight()};
  }
  Rect actionRect(uint8_t i) const {
    const auto r = seriesPanel();
    if (series_.scene() == SeriesScene::Count || series_.scene() == SeriesScene::Review) {
      const int y = series_.scene() == SeriesScene::Count ? r.y + seriesScale(CROSSINK_APP_CAP_TOUCH ? 170 : 218)
                                                          : r.y + r.height - seriesScale(68);
      return Rect{r.x + seriesScale(18) + i * (r.width - seriesScale(27)) / 2, y, (r.width - seriesScale(45)) / 2,
                  seriesScale(50)};
    }
    return Rect{r.x + seriesScale(18), r.y + r.height - seriesScale(18) - (actionCount() - i) * seriesScale(59),
                r.width - seriesScale(36), seriesScale(50)};
  }
  static bool inside(Rect r, int x, int y) { return x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height; }
  const char* notice() const {
    switch (series_.flow().notice) {
      case SeriesNotice::ConnectFailed:
        return tr(STR_SERIES_CONNECT_FAILED);
      case SeriesNotice::FileUnavailable:
        return tr(STR_SERIES_FILE_UNAVAILABLE);
      case SeriesNotice::PreviewError:
        return tr(STR_SERIES_PREVIEW_ERROR);
      case SeriesNotice::Incomplete:
        return tr(STR_SERIES_INCOMPLETE_NOTICE);
      case SeriesNotice::NoEligible:
        return tr(STR_SERIES_NO_ELIGIBLE);
      default:
        return tr(STR_SERIES_UNAVAILABLE);
    }
  }
  const char* stateLabel(SeriesItemState state) const {
    switch (state) {
      case SeriesItemState::Queued:
        return tr(STR_SERIES_QUEUED);
      case SeriesItemState::Running:
        return tr(STR_SERIES_RUNNING);
      case SeriesItemState::Paused:
        return tr(STR_SERIES_PAUSED);
      case SeriesItemState::Complete:
        return tr(STR_SERIES_COMPLETE);
      case SeriesItemState::Canceled:
        return tr(STR_SERIES_CANCELED);
      case SeriesItemState::Unsupported:
        return tr(STR_SERIES_UNSUPPORTED);
      default:
        return tr(STR_SERIES_FAILED_STATUS);
    }
  }
  void sizeText(char* out, size_t capacity, uint64_t bytes, bool known, bool fileSize = true) const {
    if (!known) {
      std::snprintf(out, capacity, "%s", tr(STR_SERIES_SIZE_UNKNOWN));
      return;
    }
    const uint64_t unit = bytes >= 1073741824                                   ? 1073741824
                          : (bytes >= 1048576 || (fileSize && bytes >= 104858)) ? 1048576
                          : bytes >= 1024                                       ? 1024
                                                                                : 1;
    const char* suffix = unit == 1073741824 ? tr(STR_SERIES_GB)
                         : unit == 1048576  ? tr(STR_SERIES_MB)
                         : unit == 1024     ? tr(STR_SERIES_KB)
                                            : tr(STR_SERIES_B);
    if (bytes == unit && unit > 1)
      std::snprintf(out, capacity, tr(STR_SERIES_SIZE_DECIMAL_FORMAT), double(bytes) / unit, suffix);
    else if (bytes % unit == 0)
      std::snprintf(out, capacity, tr(STR_SERIES_SIZE_WHOLE_FORMAT), static_cast<unsigned long long>(bytes / unit),
                    suffix);
    else
      std::snprintf(out, capacity, tr(STR_SERIES_SIZE_DECIMAL_FORMAT), double(bytes) / unit, suffix);
  }
  Rect headerSearchTarget() const {
    const auto s = safe();
    const int cx = s.x + s.width - seriesScale(40), cy = s.y + seriesScale(16) + seriesScale(29);
    return Rect{cx - 25, cy - 25, 50, 50};
  }
  void header() const {
    auto s = safe();
    s.y += seriesScale(16);
    const auto target = headerSearchTarget();
    const int cx = target.x + target.width / 2, cy = target.y + target.height / 2;
    const int y = cy, x = s.x + seriesScale(18);
    const auto backInk = renderer.getTextVerticalBounds(SERIES_CAPTION_FONT_ID, tr(STR_BACK));
    const auto titleInk = renderer.getTextVerticalBounds(titleFont(), tr(STR_SERIES_TITLE), EpdFontFamily::BOLD);
    renderer.drawLine(x + seriesScale(8), y - seriesScale(9), x, y);
    renderer.drawLine(x, y, x + seriesScale(8), y + seriesScale(9));
    text(Rect{s.x + seriesScale(36), cy - (backInk.top + backInk.bottom) / 2, seriesScale(62),
              std::max(smallLine(), backInk.bottom)},
         tr(STR_BACK), false, SERIES_CAPTION_FONT_ID);
    text(Rect{s.x + seriesScale(90), cy - (titleInk.top + titleInk.bottom) / 2, s.width - seriesScale(158),
              std::max(titleLine_, titleInk.bottom)},
         tr(STR_SERIES_TITLE), true, titleFont());
    const int radius = seriesScale(24);
    const bool focused =
        !CROSSINK_APP_CAP_TOUCH && series_.scene() == SeriesScene::Page && focus_ == series_.members().count + 1;
    renderer.drawRoundedRect(cx - radius, cy - radius, 2 * radius, 2 * radius, focused ? 2 : 1, radius, true);
    const int z = seriesScale(16), sx = cx - seriesScale(10), sy = cy - seriesScale(10);
    renderer.drawRoundedRect(sx, sy, z, z, 1, z / 2, true);
    renderer.drawLine(sx + z - 1, sy + z - 1, sx + z + seriesScale(7), sy + z + seriesScale(7), 1, true);
    renderer.drawLine(s.x, s.y + seriesScale(58), s.x + s.width, s.y + seriesScale(58));
  }
  int totalsBarY(int y) const {
    return y + std::max(renderer.getLineHeight(SERIES_NUMBER_FONT_ID), smallLine() + seriesScale(8)) + seriesScale(6);
  }
  void totals(int y) const {
    const auto s = safe();
    const auto& plan = series_.plan();
    const uint32_t values[] = {plan.total, plan.read, plan.reading};
    const char* labels[] = {tr(STR_SERIES_BOOKS), tr(STR_SERIES_READ), tr(STR_SERIES_READING)};
    for (uint8_t i = 0; i < 3; ++i) {
      const int x = s.x + seriesScale(16) + i * seriesScale(90);
      char n[16];
      std::snprintf(n, sizeof(n), "%lu", static_cast<unsigned long>(values[i]));
      const int width = seriesScale(90);
      int valueFont = SERIES_NUMBER_FONT_ID;
      if (renderer.getTextWidth(valueFont, n, EpdFontFamily::BOLD) > width) valueFont = UI_10_FONT_ID;
      const int valueWidth = renderer.getTextWidth(valueFont, n, EpdFontFamily::BOLD);
      const bool stacked =
          valueWidth + seriesScale(6) + renderer.getTextWidth(SERIES_CAPTION_FONT_ID, labels[i]) > width;
      text(Rect{x, y, width, seriesScale(42)}, n, true, valueFont);
      text(Rect{x + (stacked ? 0 : valueWidth + seriesScale(6)),
                y + (stacked ? renderer.getLineHeight(valueFont)
                             : renderer.getFontAscenderSize(valueFont) -
                                   renderer.getFontAscenderSize(SERIES_CAPTION_FONT_ID)),
                width - (stacked ? 0 : valueWidth + seriesScale(6)), seriesScale(24)},
           labels[i], false, SERIES_CAPTION_FONT_ID);
    }
    if (plan.progressCount && plan.total) {
      const int barY = totalsBarY(y);
      const unsigned count = std::min<uint8_t>(plan.progressCount, SeriesProgressMax);
      if (!count) return;
      const int width = (s.width - seriesScale(32)) / count;
      for (unsigned i = 0; i < count; ++i) {
        const int x = s.x + seriesScale(16) + i * width, height = seriesScale(7);
        renderer.drawRoundedRect(x, barY, width - seriesScale(3), height, 1, seriesScale(3), true);
        const auto& segment = plan.progress[i];
        const bool reading = segment.status == QueryShow::Reading || segment.status == QueryShow::Rereading;
        const int fill = segment.status == QueryShow::Finished ? width - seriesScale(3) - 2
                         : reading ? (width - seriesScale(3) - 2) * (segment.known ? segment.percent : 50) / 100
                                   : 0;
        if (fill > 0 && height > 2) renderer.fillRect(x + 1, barY + 1, fill, height - 2);
      }
    }
  }
  void page() const {
    const auto s = safe();
    const auto& plan = series_.plan();
    const auto& members = series_.members();
    if (members.availability != Availability::Ready || !members.count) {
      const char* message = series_.loading()                                ? tr(STR_SERIES_LOADING)
                            : members.availability == Availability::NotSaved ? tr(STR_SERIES_NOT_SAVED)
                            : members.availability == Availability::Ready    ? tr(STR_SERIES_EMPTY)
                                                                             : tr(STR_SERIES_PAGE_ERROR);
      text(Rect{s.x + seriesScale(16), seriesTop() + seriesScale(20), s.width - seriesScale(32), seriesScale(100)},
           message);
      if (series_.retryEnabled()) {
        const auto retry = retryButton();
        actionOutline(retry, !CROSSINK_APP_CAP_TOUCH && focus_ == 0);
        buttonText(retry, tr(STR_RETRY));
      }
      return;
    }
    text(Rect{s.x + seriesScale(16), seriesTop() + seriesScale(8), s.width - seriesScale(32), seriesScale(36)},
         plan.title[0] ? plan.title.data() : tr(STR_SERIES_TITLE), true, titleFont());
    const char* subtitle = plan.membershipIncomplete ? tr(STR_SERIES_MEMBERSHIP_INCOMPLETE)
                           : plan.offline            ? tr(STR_SERIES_OFFLINE_CACHED)
                           : plan.remainingSaved     ? tr(STR_SERIES_REMAINING_SAVED)
                           : !plan.deviceKnown       ? tr(STR_SERIES_DEVICE_UNKNOWN)
                                                     : plan.author.data();
    text(Rect{s.x + seriesScale(16), seriesTop() + seriesScale(44), s.width - seriesScale(32), seriesScale(24)},
         subtitle, false, SERIES_CAPTION_FONT_ID);
    if (plan.countsKnown || plan.availability == Availability::Ready) totals(seriesTop() + seriesScale(70));
    const Rect card{s.x + seriesScale(16), cardTop(), s.width - seriesScale(32),
                    cardButtonY() + seriesScale(60) - cardTop()};
    outline(card);
    char title[96];
    if (plan.count)
      std::snprintf(title, sizeof(title), tr(STR_SERIES_NEXT_TITLE_FORMAT), unsigned(plan.count));
    else
      std::snprintf(title, sizeof(title), "%s", tr(STR_SERIES_QUEUE));
    text(Rect{card.x + seriesScale(12), card.y + seriesScale(24), card.width - seriesScale(60), seriesTextLine()},
         title, true);
    // The approved card's download icon stays at its top right.
    const int ix = card.x + card.width - seriesScale(30), iy = card.y + seriesScale(26);
    renderer.drawLine(ix, iy, ix, iy + seriesScale(18), 2, true);
    renderer.drawLine(ix - seriesScale(7), iy + seriesScale(11), ix, iy + seriesScale(18));
    renderer.drawLine(ix, iy + seriesScale(18), ix + seriesScale(7), iy + seriesScale(11));
    renderer.drawLine(ix - seriesScale(8), iy + seriesScale(18), ix - seriesScale(8), iy + seriesScale(25));
    renderer.drawLine(ix - seriesScale(8), iy + seriesScale(25), ix + seriesScale(8), iy + seriesScale(25));
    renderer.drawLine(ix + seriesScale(8), iy + seriesScale(25), ix + seriesScale(8), iy + seriesScale(18));
    const char* summary = plan.count                  ? tr(STR_SERIES_KEEP_READY)
                          : plan.membershipIncomplete ? tr(STR_SERIES_MEMBERSHIP_INCOMPLETE)
                          : !plan.deviceKnown         ? tr(STR_SERIES_DEVICE_UNKNOWN)
                          : plan.count                ? tr(STR_SERIES_KEEP_READY)
                          : plan.earlierKnown         ? tr(STR_SERIES_EARLIER_PENDING)
                          : plan.remainingSaved       ? tr(STR_SERIES_REMAINING_SAVED)
                                                      : tr(STR_SERIES_NO_REMAINING);
    text(Rect{card.x + seriesScale(12), card.y + seriesTextLine() + seriesScale(48), card.width - seriesScale(24),
              seriesScale(24)},
         summary, false, SERIES_CAPTION_FONT_ID);
    for (uint8_t i = 0; i < std::min<uint8_t>(plan.count, 3); ++i) {
      if (plan.next[i].positionKnown) {
        char number[24];
        std::snprintf(number, sizeof(number), "%.6g", plan.next[i].position);
        std::snprintf(title, sizeof(title), tr(STR_SERIES_PAIR_FORMAT), number, plan.next[i].book.title.data());
      } else
        std::snprintf(title, sizeof(title), "%s", plan.next[i].book.title.data());
      text(Rect{card.x + seriesScale(12), cardListTop() + i * cardTitleStep(), card.width - seriesScale(24),
                smallLine()},
           title, true, SERIES_META_FONT_ID);
    }
    const auto d = downloadButton();
    actionOutline(d, !CROSSINK_APP_CAP_TOUCH && focus_ == 0, !series_.downloadEnabled());
    buttonText(d, plan.membershipIncomplete    ? tr(STR_SERIES_MEMBERSHIP_INCOMPLETE)
                  : !series_.downloadEnabled() ? tr(STR_SERIES_UNAVAILABLE_SOURCE)
                  : plan.count                 ? tr(STR_SERIES_DOWNLOAD_NEXT)
                                               : tr(STR_SERIES_DOWNLOADS_MORE));
    text(Rect{s.x + seriesScale(16), membersY(), seriesScale(130), memberLine_}, tr(STR_SERIES_MEMBERS), true,
         memberFont());
    char range[72];
    std::snprintf(range, sizeof(range), tr(STR_SERIES_RANGE_FORMAT), static_cast<unsigned long>(series_.offset() + 1),
                  static_cast<unsigned long>(series_.offset() + members.count),
                  static_cast<unsigned long>(members.matchedBooks));
    const int rangeWidth = renderer.getTextWidth(SERIES_CAPTION_FONT_ID, range);
    text(Rect{s.x + s.width - seriesScale(16) - rangeWidth,
              membersY() + renderer.getFontAscenderSize(memberFont()) -
                  renderer.getFontAscenderSize(SERIES_CAPTION_FONT_ID),
              rangeWidth, smallLine()},
         range, false, SERIES_CAPTION_FONT_ID);
    int numberWidth = 25;
    for (uint8_t i = 0; i < std::min<uint16_t>(members.count, 3); ++i) {
      char number[24];
      if (members.entries[i].seriesPositionKnown)
        std::snprintf(number, sizeof(number), "%.6g", members.entries[i].seriesPosition);
      else
        std::snprintf(number, sizeof(number), "–");
      numberWidth = std::max(numberWidth, renderer.getTextWidth(memberFont(), number, EpdFontFamily::BOLD));
    }
    for (uint8_t i = 0; i < std::min<uint16_t>(members.count, 3); ++i) {
      const auto r = pageRow(i);
      const auto& row = members.entries[i];
      if (!CROSSINK_APP_CAP_TOUCH && focus_ == i + 1) outline(r, 2);
      renderer.drawLine(r.x, r.y + r.height, r.x + r.width, r.y + r.height);
      char number[24];
      if (row.seriesPositionKnown)
        std::snprintf(number, sizeof(number), "%.6g", row.seriesPosition);
      else
        std::snprintf(number, sizeof(number), "–");
      const auto numberInk = renderer.getTextVerticalBounds(memberFont(), number, EpdFontFamily::BOLD);
      text(
          Rect{r.x + seriesScale(4), r.y + (r.height - numberInk.bottom - numberInk.top) / 2, numberWidth, memberLine_},
          number, true, memberFont());
      int left = r.x + seriesScale(4) + numberWidth + 10;
#if !CROSSINK_APP_CAP_HOME_EDITOR_SETTINGS
      const Rect c{left, r.y + (r.height - seriesScale(72)) / 2, seriesScale(48), seriesScale(72)};
      if (references_[i].id) {
        for (int y = 0; y < c.height; ++y)
          for (int x = 0; x < c.width; ++x) {
            const unsigned sx = c.width > 1 ? x * 47 / (c.width - 1) : 0,
                           sy = c.height > 1 ? y * 71 / (c.height - 1) : 0;
            if (!(covers_[i][sy * 6 + sx / 8] & (0x80 >> (sx % 8)))) renderer.drawPixel(c.x + x, c.y + y);
          }
      } else
        renderer.drawRect(c.x, c.y, c.width, c.height);
      left += c.width + 10;
#endif
      const int width = r.x + r.width - left - seriesScale(6);
      const bool reading = row.status == QueryShow::Reading || row.status == QueryShow::Rereading;
      const int titleY = r.y + (r.height - memberTextHeight(reading)) / 2;
      const int publisherY = titleY + memberLine_ + seriesScale(4);
      const int stateY = publisherY + renderer.getLineHeight(SERIES_CAPTION_FONT_ID) + seriesScale(4);
      text(Rect{left, titleY, width, memberLine_}, row.book.title.data(), true, memberFont());
      text(Rect{left, publisherY, width, captionLine_}, row.label.data(), false, SERIES_CAPTION_FONT_ID);
      const char* status = row.status == QueryShow::Finished ? tr(STR_SERIES_READ_STATUS)
                           : (row.status == QueryShow::Reading || row.status == QueryShow::Rereading)
                               ? tr(STR_SERIES_READING)
                           : row.onDevice == QueryTruth::Yes ? tr(STR_SERIES_ON_DEVICE)
                           : row.onDevice == QueryTruth::No  ? tr(STR_SERIES_ON_SERVER)
                                                             : tr(STR_SERIES_UNKNOWN);
      const bool progressKnown = reading && row.book.readingKnown && row.book.pages;
      const auto percentage =
          progressKnown ? std::min<uint64_t>(100, uint64_t(row.book.page) * 100 / row.book.pages) : 0;
      if (progressKnown) {
        std::snprintf(title, sizeof(title), tr(STR_SERIES_PROGRESS_FORMAT), unsigned(percentage),
                      tr(STR_SERIES_READING));
        status = title;
      }
      text(Rect{left, stateY, width, captionLine_}, status, false, SERIES_CAPTION_FONT_ID);
      if (reading) {
        const int bar = std::min(width, seriesScale(110)), height = std::max(3, seriesScale(4));
        const int y = stateY + captionLine_ + 6;
        renderer.drawRect(left, y, bar, height);
        if (percentage && bar > 2 && height > 2)
          renderer.fillRect(left + 1, y + 1, (bar - 2) * percentage / 100, height - 2);
      }
    }
  }
  const char* actionLabel(uint8_t i, char* buffer, size_t capacity) const {
    switch (series_.scene()) {
      case SeriesScene::Preview:
        if (i == 0) {
          if (series_.flow().online) return tr(STR_SERIES_REVIEW_FILES);
          std::snprintf(buffer, capacity, tr(STR_SERIES_CONNECT_FORMAT), unsigned(series_.flow().plan.count));
          return buffer;
        }
        if (i == 1) return tr(STR_SERIES_CHANGE_NUMBER);
        if (i == 2 && series_.flow().plan.earlierKnown) {
          std::snprintf(buffer, capacity, tr(STR_SERIES_EARLIER_FORMAT), series_.flow().plan.earlier.book.title.data());
          return buffer;
        }
        return tr(STR_CANCEL);
      case SeriesScene::Review:
        return i == 0 ? tr(STR_SERIES_START) : tr(STR_BACK);
      case SeriesScene::Count:
        return i == 0 ? tr(STR_CANCEL) : tr(STR_SERIES_REVIEW);
      case SeriesScene::Files:
        return tr(STR_CANCEL);
      case SeriesScene::Queue:
        if (series_.flow().queue.paused && i == 0)
          return series_.flow().online ? tr(STR_SERIES_RESUME) : tr(STR_SERIES_CONNECT_RESUME);
        if (!series_.flow().queue.complete && i == unsigned(series_.flow().queue.paused))
          return tr(STR_SERIES_CANCEL_REMAINING);
        return tr(STR_BACK);
      default:
        return tr(STR_BACK);
    }
  }
  unsigned controlCount() const {
    if (series_.scene() == SeriesScene::Count) return 2;
    return series_.scene() == SeriesScene::Page ? series_.members().count + 2 : rowCount() + actionCount();
  }
  // Read-only rows remain navigable so button users can scroll every item.
  // activate() and the controller independently reject disabled row actions.
  bool focusable(uint8_t control) const { return control < controlCount(); }
  uint8_t firstFocus() const {
    if (series_.scene() == SeriesScene::Count) return 1;
    if (series_.scene() == SeriesScene::Page) return series_.members().count ? 1 : 0;
    if (series_.scene() == SeriesScene::Preview || series_.scene() == SeriesScene::Review) return rowCount();
    if (series_.scene() == SeriesScene::Queue) {
      for (uint8_t i = 0; i < rowCount(); ++i)
        if (series_.flow().queue.items[i].cancelable) return i;
      return rowCount();
    }
    return 0;
  }
  void alignFocusScroll() {
    if (focus_ < rowCount()) {
      if (focus_ < scroll_) scroll_ = focus_;
      if (focus_ >= scroll_ + visibleRows()) scroll_ = focus_ - visibleRows() + 1;
    }
  }
  void moveFocus(int direction) {
    const int next = int(focus_) + direction;
    if (next >= 0 && unsigned(next) < controlCount()) focus_ = next;
    alignFocusScroll();
  }
  void activate(uint8_t control) {
    const auto count = rowCount();
    if (series_.scene() == SeriesScene::Page) {
      if (control == 0) {
        if (series_.retryEnabled())
          series_.retrySeriesPage();
        else if (!series_.members().count)
          series_.backSeries();
        else
          series_.downloadSeries();
      } else if (control <= series_.members().count)
        series_.openSeriesBook(control - 1);
      else
        series_.searchSeries();
      return;
    }
    if (series_.scene() == SeriesScene::Count) {
      if (control == 0)
        series_.backSeries();
      else
        series_.submitSeriesCount();
      return;
    }
    if (control < count) {
      if (series_.scene() == SeriesScene::Files) series_.chooseSeriesFile(control);
      if (series_.scene() == SeriesScene::Queue) series_.cancelSeriesRow(control);
      return;
    }
    const uint8_t action = control - count;
    switch (series_.scene()) {
      case SeriesScene::Preview:
        if (action == 0)
          series_.reviewSeries();
        else if (action == 1)
          series_.changeSeriesCount();
        else if (action == 2 && series_.flow().plan.earlierKnown)
          series_.earlierSeriesGap();
        else
          series_.backSeries();
        break;
      case SeriesScene::Review:
        if (action == 0)
          series_.startSeriesFiles();
        else
          series_.backSeries();
        break;
      case SeriesScene::Queue:
        if (series_.flow().queue.paused && action == 0)
          series_.resumeSeriesQueue();
        else if (!series_.flow().queue.complete && action == unsigned(series_.flow().queue.paused))
          series_.cancelSeriesRemaining();
        else
          series_.backSeries();
        break;
      default:
        series_.backSeries();
        break;
    }
  }
  void sheet() const {
    const auto r = seriesPanel();
    renderer.fillRect(r.x, r.y, r.width, r.height, false);
    if (series_.scene() != SeriesScene::Count) outline(r, 2);
    char buffer[160];
    const auto& f = series_.flow();
    const char* title = tr(STR_SERIES_QUEUE);
    switch (series_.scene()) {
      case SeriesScene::Queue:
        std::snprintf(buffer, sizeof(buffer), tr(STR_SERIES_QUEUE_TITLE_FORMAT), f.queue.saved, f.queue.skipped,
                      f.queue.failed, f.queue.count);
        title = buffer;
        break;
      case SeriesScene::Preview:
        std::snprintf(buffer, sizeof(buffer), tr(STR_SERIES_PREVIEW_FORMAT), unsigned(f.plan.requested),
                      unsigned(f.plan.count));
        title = buffer;
        break;
      case SeriesScene::Files:
        std::snprintf(buffer, sizeof(buffer), tr(STR_SERIES_CHOOSE_FORMAT), f.plan.next[f.fileRow].book.title.data());
        title = buffer;
        break;
      case SeriesScene::Review:
        std::snprintf(buffer, sizeof(buffer), tr(STR_SERIES_REVIEW_FORMAT), unsigned(f.plan.count));
        title = buffer;
        break;
      case SeriesScene::Count:
        title = tr(STR_SERIES_COUNT);
        break;
      case SeriesScene::Notice:
        if (f.notice == SeriesNotice::FileUnavailable && f.plan.next[f.fileRow].book.title[0]) {
          std::snprintf(buffer, sizeof(buffer), tr(STR_SERIES_PAIR_FORMAT), f.plan.next[f.fileRow].book.title.data(),
                        notice());
          title = buffer;
        } else
          title = notice();
        break;
      default:
        break;
    }
    wrapped(Rect{r.x + seriesScale(12), r.y + seriesScale(12), r.width - seriesScale(50),
                 series_.scene() == SeriesScene::Notice ? r.height - seriesScale(90) : seriesScale(68)},
            title, series_.scene() == SeriesScene::Notice ? 8 : 2, true, titleFont());
    if (series_.scene() == SeriesScene::Notice) {
      const int x = r.x + r.width - seriesScale(24), y = r.y + seriesScale(14);
      renderer.drawLine(x, y, x - seriesScale(10), y + seriesScale(20));
      renderer.drawLine(x - seriesScale(10), y + seriesScale(20), x + seriesScale(10), y + seriesScale(20));
      renderer.drawLine(x + seriesScale(10), y + seriesScale(20), x, y);
      renderer.drawLine(x, y + seriesScale(7), x, y + seriesScale(12), 2);
      renderer.fillRect(x, y + seriesScale(15), 2, 2);
    } else {
      renderer.drawLine(r.x + r.width - seriesScale(24), r.y + seriesScale(14), r.x + r.width - seriesScale(24),
                        r.y + seriesScale(32), 2, true);
      renderer.drawLine(r.x + r.width - seriesScale(30), r.y + seriesScale(26), r.x + r.width - seriesScale(24),
                        r.y + seriesScale(32));
      renderer.drawLine(r.x + r.width - seriesScale(24), r.y + seriesScale(32), r.x + r.width - seriesScale(18),
                        r.y + seriesScale(26));
      renderer.drawLine(r.x + r.width - seriesScale(32), r.y + seriesScale(31), r.x + r.width - seriesScale(32),
                        r.y + seriesScale(38));
      renderer.drawLine(r.x + r.width - seriesScale(32), r.y + seriesScale(38), r.x + r.width - seriesScale(16),
                        r.y + seriesScale(38));
      renderer.drawLine(r.x + r.width - seriesScale(16), r.y + seriesScale(38), r.x + r.width - seriesScale(16),
                        r.y + seriesScale(31));
    }
    if (series_.scene() == SeriesScene::Count) {
      const Rect field{r.x + seriesScale(16), r.y + seriesScale(70), r.width - seriesScale(32), seriesScale(74)};
      renderer.drawRect(field.x, field.y, field.width, field.height, CROSSINK_APP_CAP_TOUCH ? 1 : 2, true);
      const int numberWidth = renderer.getTextWidth(SERIES_NUMBER_FONT_ID, series_.countText());
      text(Rect{field.x + (field.width - numberWidth) / 2, field.y + seriesScale(10), numberWidth,
                renderer.getLineHeight(SERIES_NUMBER_FONT_ID)},
           series_.countText(), true, SERIES_NUMBER_FONT_ID);
#if CROSSINK_APP_CAP_TOUCH
      static const char* digits[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", nullptr, "0", nullptr};
      for (uint8_t i = 0; i < 12; ++i) {
        const auto k = keyRect(i);
        renderer.drawRoundedRect(k.x, k.y, k.width, k.height, 1, seriesScale(5), true);
        if (i == 9) {
          const int x = k.x + k.width / 2 - seriesScale(10), y = k.y + k.height / 2;
          renderer.drawLine(x - seriesScale(8), y, x, y - seriesScale(9));
          renderer.drawLine(x - seriesScale(8), y, x, y + seriesScale(9));
          renderer.drawLine(x, y - seriesScale(9), x + seriesScale(22), y - seriesScale(9));
          renderer.drawLine(x, y + seriesScale(9), x + seriesScale(22), y + seriesScale(9));
          renderer.drawLine(x + seriesScale(22), y - seriesScale(9), x + seriesScale(22), y + seriesScale(9));
          renderer.drawLine(x + seriesScale(5), y - seriesScale(5), x + seriesScale(15), y + seriesScale(5));
          renderer.drawLine(x + seriesScale(15), y - seriesScale(5), x + seriesScale(5), y + seriesScale(5));
        } else if (i == 11) {
          const int x = k.x + k.width / 2, y = k.y + k.height / 2;
          renderer.drawLine(x - seriesScale(8), y, x - seriesScale(2), y + seriesScale(6), 2);
          renderer.drawLine(x - seriesScale(2), y + seriesScale(6), x + seriesScale(9), y - seriesScale(8), 2);
        } else {
          const int width = renderer.getTextWidth(memberFont(), digits[i]);
          const auto ink = renderer.getTextVerticalBounds(memberFont(), digits[i]);
          text(Rect{k.x + (k.width - width) / 2, k.y + (k.height - ink.top - ink.bottom) / 2, width, memberLine_},
               digits[i], false, memberFont());
        }
      }
#else
      wrapped(Rect{r.x + seriesScale(16), field.y + field.height + seriesScale(12), r.width - seriesScale(32),
                   2 * smallLine()},
              tr(STR_SERIES_NUMBER_HINT), 2, false, SERIES_CAPTION_FONT_ID);
#endif
    }
    if (series_.scene() == SeriesScene::Queue) {
      const uint8_t counts[] = {f.queue.saved, f.queue.skipped, f.queue.failed};

      const char* labels[] = {tr(STR_SERIES_SAVED), tr(STR_SERIES_SKIPPED), tr(STR_SERIES_FAILED),
                              tr(STR_SERIES_REQUESTED)};
      for (uint8_t i = 0; i < 4; ++i) {
        const int width = (r.width - seriesScale(24)) / 4;
        const int x = r.x + seriesScale(12) + i * width;
        char number[8];
        std::snprintf(number, sizeof(number), "%u", i == 3 ? f.queue.count : counts[i]);
        text(Rect{x, r.y + seriesScale(76), width, renderer.getLineHeight(SERIES_NUMBER_FONT_ID)}, number, true,
             SERIES_NUMBER_FONT_ID);
        text(Rect{x, r.y + seriesScale(122), width, smallLine()}, labels[i], false, SERIES_CAPTION_FONT_ID);
      }
    }
    if (series_.scene() == SeriesScene::Queue && f.queue.paused) {
      text(Rect{r.x + seriesScale(12), r.y + seriesScale(148), r.width - seriesScale(24), smallLine()},
           !f.online                ? tr(STR_SERIES_QUEUE_OFFLINE)
           : series_.signInNeeded() ? tr(STR_SERIES_QUEUE_AUTH)
                                    : tr(STR_SERIES_QUEUE_RETAINED),
           false, SERIES_CAPTION_FONT_ID);
    }
    const auto rows = rowCount();
    for (uint8_t i = scroll_; i < rows && i < scroll_ + visibleRows(); ++i) {
      const auto row = sheetRow(i);
      const char* titleText = f.plan.next[i].book.title.data();
      if (series_.scene() == SeriesScene::Files)
        titleText = f.files[i].label.data();
      else if (series_.scene() == SeriesScene::Queue)
        titleText = f.queue.items[i].title.data();
      if (series_.scene() == SeriesScene::Preview && f.plan.next[i].positionKnown) {
        char number[24];
        std::snprintf(number, sizeof(number), "%.6g", f.plan.next[i].position);
        std::snprintf(buffer, sizeof(buffer), tr(STR_SERIES_PAIR_FORMAT), number, titleText);
        titleText = buffer;
      }
      const bool choice =
          series_.scene() == SeriesScene::Files ||
          (series_.scene() == SeriesScene::Queue && f.queue.items[i].state != SeriesItemState::Complete &&
           f.queue.items[i].state != SeriesItemState::Unsupported);
      const int contentHeight = row.height - (choice ? seriesScale(6) : 0);
      if (choice || (!CROSSINK_APP_CAP_TOUCH && focus_ == i))
        renderer.drawRoundedRect(row.x, row.y, row.width, contentHeight, !CROSSINK_APP_CAP_TOUCH && focus_ == i ? 2 : 1,
                                 CROSSINK_APP_CAP_HOME_EDITOR_SETTINGS ? 0 : seriesScale(6), true);
      char itemTitle[160];
      std::snprintf(itemTitle, sizeof(itemTitle), "%s", titleText);
      if (series_.scene() == SeriesScene::Review && f.review[i].supported)
        std::snprintf(itemTitle, sizeof(itemTitle), tr(STR_SERIES_PAIR_FORMAT), titleText, f.review[i].label.data());
      if (series_.scene() == SeriesScene::Preview)
        sizeText(buffer, sizeof(buffer), f.review[i].bytes, f.review[i].sizeKnown);
      else if (series_.scene() == SeriesScene::Files)
        sizeText(buffer, sizeof(buffer), f.files[i].bytes, f.files[i].sizeKnown);
      else if (series_.scene() == SeriesScene::Review) {
        if (!f.review[i].supported)
          std::snprintf(buffer, sizeof(buffer), "%s", tr(STR_SERIES_NO_FILE));
        else
          sizeText(buffer, sizeof(buffer), f.review[i].bytes, f.review[i].sizeKnown);
      } else {
        const auto& item = f.queue.items[i];
        char progress[64];
        const auto* state = item.error == DownloadError::NoSpace ? tr(STR_SERIES_CARD_FULL) : stateLabel(item.state);
        if (item.state == SeriesItemState::Running && i == f.queue.active) {
          if (f.queue.totalKnown && f.queue.total)
            std::snprintf(progress, sizeof(progress), tr(STR_SERIES_PERCENT_FORMAT),
                          unsigned(std::min<uint64_t>(100, f.queue.received * 100 / f.queue.total)));
          else {
            char received[40];
            sizeText(received, sizeof(received), f.queue.received, true, false);
            std::snprintf(progress, sizeof(progress), tr(STR_SERIES_RECEIVED_FORMAT), received);
          }
          state = progress;
        }
        // Android retains the Cancel label on canceled rows; activation stays disabled.
        if (item.cancelable || item.state == SeriesItemState::Canceled)
          std::snprintf(buffer, sizeof(buffer), tr(STR_SERIES_CANCEL_SUFFIX_FORMAT), state);
        else
          std::snprintf(buffer, sizeof(buffer), "%s", state);
      }
      const int detailWidth = std::min(row.width * 55 / 100, renderer.getTextWidth(SERIES_CAPTION_FONT_ID, buffer));
      const int textY = row.y + (contentHeight - smallLine()) / 2;
      text(Rect{row.x + seriesScale(5), textY, row.width - detailWidth - seriesScale(22), smallLine()}, itemTitle, true,
           SERIES_META_FONT_ID);
      text(Rect{row.x + row.width - seriesScale(5) - detailWidth,
                textY + renderer.getFontAscenderSize(SERIES_META_FONT_ID) -
                    renderer.getFontAscenderSize(SERIES_CAPTION_FONT_ID),
                detailWidth, smallLine()},
           buffer, false, SERIES_CAPTION_FONT_ID);
      if (!choice)
        renderer.drawLine(row.x, row.y + row.height - seriesScale(2), row.x + row.width,
                          row.y + row.height - seriesScale(2));
    }
    if (series_.scene() == SeriesScene::Review) {
      const auto a = actionRect(0);
      const int y = a.y - rowHeight() + (rowHeight() - memberLine_) / 2;
      text(Rect{r.x + seriesScale(18), y, r.width / 3, memberLine_}, tr(STR_SERIES_DESTINATION), true, memberFont());
      text(Rect{r.x + r.width / 3 + seriesScale(18),
                y + renderer.getFontAscenderSize(memberFont()) - renderer.getFontAscenderSize(SERIES_CAPTION_FONT_ID),
                r.width * 2 / 3 - seriesScale(36), captionLine_},
           f.review[0].destination.data(), false, SERIES_CAPTION_FONT_ID);
    }
    if (rows > visibleRows()) {
      const int x = r.x + r.width - seriesScale(6);
      const int h = visibleRows() * rowHeight();
      renderer.drawLine(x, r.y + sheetListTop(), x, r.y + sheetListTop() + h);
      renderer.fillRect(x - seriesScale(2), r.y + sheetListTop() + scroll_ * h / rows, seriesScale(4),
                        std::max(seriesScale(8), h * visibleRows() / rows));
    }
    for (uint8_t i = 0; i < actionCount(); ++i) {
      const auto a = actionRect(i);
      const bool disabled = series_.scene() == SeriesScene::Count && i == 1 && !series_.countValid();
      actionOutline(a, !CROSSINK_APP_CAP_TOUCH && series_.scene() != SeriesScene::Count && focus_ == rows + i,
                    disabled);
      buttonText(a, actionLabel(i, buffer, sizeof(buffer)), !disabled);
    }
  }
  bool prepareCovers() {
#if !CROSSINK_APP_CAP_HOME_EDITOR_SETTINGS
    if (!painted_ || series_.scene() != SeriesScene::Page) return false;
    bool changed = false;
    for (uint8_t i = 0; i < std::min<uint16_t>(series_.members().count, 3); ++i) {
      const auto key = series_.members().entries[i].book.key;
      if (!(keys_[i] == key)) {
        keys_[i] = key;
        references_[i] = {};
        covers_[i].fill(0xff);
      }
      library_.requestCover(key);
      const auto reference = library_.coverReference(key);
      if (reference.id && !(reference == references_[i]) && library_.readCover(reference, bitmap_) && bitmap_.width &&
          bitmap_.height && bitmap_.width <= CoverWidth && bitmap_.height <= CoverHeight) {
        covers_[i].fill(0xff);
        const unsigned stride = (bitmap_.width + 7) / 8;
        for (unsigned y = 0; y < 72; ++y)
          for (unsigned x = 0; x < 48; ++x) {
            const unsigned sx = x * (bitmap_.width - 1) / 47, sy = y * (bitmap_.height - 1) / 71;
            if (!(bitmap_.pixels[sy * stride + sx / 8] & (0x80 >> (sx % 8))))
              covers_[i][y * 6 + x / 8] &= ~(0x80 >> (x % 8));
          }
        references_[i] = reference;
        changed = true;
      }
    }
    return changed;
#else
    return false;
#endif
  }

 public:
  SeriesActivity(GfxRenderer& renderer, MappedInputManager& input, Library& library, const char* key, BookKey current,
                 bool known)
      : Activity("BookSyncSeries", renderer, input), library_(library), series_(library) {
    if (key && std::strlen(key) < initial_.key.size()) std::strcpy(initial_.key.data(), key);
    initial_.current = current;
    initial_.currentKnown = known;
  }
  void onEnter() override {
#ifdef SIMULATOR
    scenarioActivity = this;
#endif
    Activity::onEnter();
    // Nominal advance can omit a descender pixel; reserve the measured glyph band.
    const auto measuredLine = [&](int font) {
      GfxRenderer::TextVerticalBounds body, extent;
      const bool measured = renderer.getLatinBodyBounds(font, body, extent);
      return std::max(renderer.getLineHeight(font), measured ? extent.bottom : 0);
    };
    textLine_ = measuredLine(textFont());
    smallLine_ = measuredLine(SERIES_META_FONT_ID);
    captionLine_ = measuredLine(SERIES_CAPTION_FONT_ID);
    memberLine_ = std::max(measuredLine(memberFont()),
                           renderer.getTextVerticalBounds(memberFont(), "Hg", EpdFontFamily::BOLD).bottom);
    titleLine_ = measuredLine(titleFont());
    LOG_INF("BSS", "Series screen workspace=%u bytes", unsigned(sizeof(SeriesActivity)));
    series_.enterSeries(initial_.key.data(), initial_.current, initial_.currentKnown);
    series_.pollSeries();
    focus_ = firstFocus();
    requestUpdate();
  }
  void onExit() override {
#ifdef SIMULATOR
    if (scenarioActivity == this) scenarioActivity = nullptr;
#endif
    series_.leaveSeries();
    Activity::onExit();
  }
  void loop() override {
    RenderLock lock;
    bool changed = false;
    const auto previous = series_.scene();
    const auto previousFile = series_.flow().fileRow;
    const auto previousMembers = series_.members().count;
    const auto now = millis();
    if (now - lastPoll_ >= 1000 || series_.pending() || series_.loading()) {
      lastPoll_ = now;
      changed = series_.pollSeries();
    }
    changed = prepareCovers() || changed;
    int x = 0, y = 0;
    const bool tapped = mappedInput.wasScreenTapped(x, y);
    const auto s = safe();
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        (tapped && inside(Rect{s.x, s.y + seriesScale(16), seriesScale(100), seriesScale(58)}, x, y))) {
      series_.backSeries();
      changed = true;
    } else if (series_.scene() == SeriesScene::Count) {
      if (mappedInput.wasReleased(mappedInput.menuButton(MappedInputManager::Button::Up))) {
        series_.stepSeriesCount(1);
        changed = true;
      }
      if (mappedInput.wasReleased(mappedInput.menuButton(MappedInputManager::Button::Down))) {
        series_.stepSeriesCount(-1);
        changed = true;
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        series_.submitSeriesCount();
        changed = true;
      }
      if (tapped) {
        for (uint8_t i = 0; i < 2; ++i)
          if (inside(actionRect(i), x, y)) {
            activate(i);
            changed = true;
          }
#if CROSSINK_APP_CAP_TOUCH
        for (uint8_t i = 0; i < 12; ++i)
          if (inside(keyRect(i), x, y)) {
            if (i == 9)
              series_.eraseSeriesCount();
            else if (i == 11)
              series_.submitSeriesCount();
            else
              series_.appendSeriesCount(i == 10 ? '0' : char('1' + i));
            changed = true;
          }
#endif
      }
    } else {
      if (mappedInput.wasReleased(mappedInput.menuButton(MappedInputManager::Button::Down))) {
        moveFocus(1);
        changed = true;
      }
      if (mappedInput.wasReleased(mappedInput.menuButton(MappedInputManager::Button::Up))) {
        moveFocus(-1);
        changed = true;
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        activate(focus_);
        changed = true;
      }
      if (series_.scene() == SeriesScene::Page) {
        if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
          series_.nextSeriesPage(-1);
          changed = true;
        }
        if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
          series_.nextSeriesPage(1);
          changed = true;
        }
        if (tapped) {
          if (inside(headerSearchTarget(), x, y)) series_.searchSeries();
          if (inside(downloadButton(), x, y)) activate(0);
          if (inside(Rect{s.x, bottom(), seriesScale(80), seriesScale(50)}, x, y)) series_.nextSeriesPage(-1);
          char range[72];
          std::snprintf(range, sizeof(range), tr(STR_SERIES_RANGE_FORMAT),
                        static_cast<unsigned long>(series_.offset() + 1),
                        static_cast<unsigned long>(series_.offset() + series_.members().count),
                        static_cast<unsigned long>(series_.members().matchedBooks));
          const int nextX =
              s.x + seriesScale(16) + renderer.getTextWidth(SERIES_CAPTION_FONT_ID, range) + seriesScale(12);
          if (inside(Rect{nextX, bottom(), seriesScale(50), seriesScale(37)}, x, y)) series_.nextSeriesPage(1);
          if (series_.retryEnabled() && inside(retryButton(), x, y)) series_.retrySeriesPage();
          for (uint8_t i = 0; i < std::min<uint16_t>(series_.members().count, 3); ++i)
            if (inside(pageRow(i), x, y)) activate(i + 1);
          changed = true;
        }
      } else if (tapped) {
        for (uint8_t i = scroll_; i < rowCount() && i < scroll_ + visibleRows(); ++i)
          if (inside(sheetRow(i), x, y)) {
            activate(i);
            changed = true;
          }
        for (uint8_t i = 0; i < actionCount(); ++i)
          if (inside(actionRect(i), x, y)) {
            activate(rowCount() + i);
            changed = true;
          }
      }
    }
#if CROSSINK_APP_CAP_TOUCH
    if (series_.scene() != SeriesScene::Page && series_.scene() != SeriesScene::Count) {
      const auto swipe = mappedInput.wasSwipe();
      if (swipe == MappedInputManager::SwipeDir::Up && scroll_ + visibleRows() < rowCount()) {
        ++scroll_;
        changed = true;
      }
      if (swipe == MappedInputManager::SwipeDir::Down && scroll_) {
        --scroll_;
        changed = true;
      }
    }
#endif
    if (previous != series_.scene() || !focusable(focus_) ||
        (series_.scene() == SeriesScene::Page && !previousMembers && series_.members().count) ||
        (series_.scene() == SeriesScene::Files && previousFile != series_.flow().fileRow)) {
      focus_ = firstFocus();
      scroll_ = 0;
      alignFocusScroll();
      changed = true;
    }
    if (!series_.live())
      activityManager.popActivity();
    else if (changed)
      requestUpdate();
  }
#ifdef SIMULATOR
  bool scenarioInvariant() {
    if (!focusable(focus_) || geometryOverflow_) return false;
    for (unsigned i = 0; i < textBoxCount_; ++i) {
      const auto& a = textBoxes_[i];
      for (unsigned j = i + 1; j < textBoxCount_; ++j) {
        const auto& b = textBoxes_[j];
        if (a.layer == b.layer && a.bounds.x < b.bounds.x + b.bounds.width &&
            b.bounds.x < a.bounds.x + a.bounds.width && a.bounds.y < b.bounds.y + b.bounds.height &&
            b.bounds.y < a.bounds.y + a.bounds.height) {
          LOG_ERR("BSS", "Overlapping text geometry: %s / %s", a.label, b.label);
          return false;
        }
      }
    }
    LOG_INF("BSS", "Series text geometry PASS: %u boxes", textBoxCount_);
    if (rowCount() && series_.scene() != SeriesScene::Page) {
      const uint8_t last = std::min<unsigned>(rowCount(), scroll_ + visibleRows()) - 1;
      if (sheetRow(last).y + rowHeight() + seriesScale(6) > actionRect(0).y) {
        LOG_ERR("BSS", "Series list window overlaps action area");
        return false;
      }
    }

    if (series_.scene() == SeriesScene::Files && focus_ != 0) return false;
    if (series_.scene() == SeriesScene::Count && focus_ != 1) return false;
    if ((series_.scene() == SeriesScene::Preview || series_.scene() == SeriesScene::Review) &&
        rowCount() > visibleRows()) {
      const auto savedFocus = focus_, savedScroll = scroll_;
      focus_ = rowCount();
      moveFocus(-1);
      bool reached = focus_ == rowCount() - 1 && focus_ >= scroll_ && focus_ < scroll_ + visibleRows();
      while (focus_) moveFocus(-1);
      reached = reached && scroll_ == 0;
      focus_ = savedFocus;
      scroll_ = savedScroll;
      if (!reached) return false;
    }
#if !CROSSINK_APP_CAP_HOME_EDITOR_SETTINGS
    if (series_.scene() == SeriesScene::Page && series_.members().count) {
      for (uint8_t i = 0; i < std::min<uint16_t>(series_.members().count, 3); ++i)
        if (references_[i].id) {
          const auto r = pageRow(i);
          const int x = r.x + seriesScale(4) + 25 + 10 + seriesScale(48) - 1,
                    y = r.y + (r.height - seriesScale(72)) / 2, height = seriesScale(72);
          unsigned edge = 0;
          for (int yy = y; yy < y + height; ++yy)
            if (renderer.isPixelBlack(x, yy)) ++edge;
          if (edge < unsigned(height / 2)) return false;
        }
    }
#endif
    return true;
  }
  void scenarioBegin(bool count, const char* typed) {
    series_.downloadSeries();
    if (count) {
      series_.changeSeriesCount();
      if (typed) {
        while (*series_.countText()) series_.eraseSeriesCount();
        for (const char* byte = typed; *byte; ++byte) series_.appendSeriesCount(*byte);
      }
    }
    focus_ = firstFocus();
    scroll_ = 0;
    alignFocusScroll();
    requestUpdate();
  }
#endif
  void render(RenderLock&&) override {
    if (!series_.live()) return;
#ifdef SIMULATOR
    ++paints;
    textBoxCount_ = 0;
    textLayer_ = 0;
    geometryOverflow_ = false;
#endif
    renderer.clearScreen();
    header();
    page();
    if (series_.scene() != SeriesScene::Page) {
      // A 1-bit veil preserves the cached page with one framebuffer. Only
      // existing pixels at a fixed pattern are whitened; no backdrop copy.
      const auto area = safe();
      for (int y = seriesTop(); y < bottom(); ++y)
        for (int x = area.x; x < area.x + area.width; ++x)
          if ((x & 3) || (y & 3)) renderer.drawPixel(x, y, false);
#ifdef SIMULATOR
      textLayer_ = 1;
#endif
      sheet();
    }
#ifdef SIMULATOR
    textLayer_ = 0;
#endif
    const auto s = safe();
    renderer.drawLine(s.x, bottom(), s.x + s.width, bottom());
    char buffer[72];
    const auto first = series_.members().count ? series_.offset() + 1 : 0;
    std::snprintf(buffer, sizeof(buffer), tr(STR_SERIES_RANGE_FORMAT), static_cast<unsigned long>(first),
                  static_cast<unsigned long>(series_.offset() + series_.members().count),
                  static_cast<unsigned long>(series_.members().matchedBooks));
    const int footerY = bottom() + seriesScale(8);
    if (series_.scene() == SeriesScene::Page && series_.members().count) {
      char pageLabel[88];
      std::snprintf(pageLabel, sizeof(pageLabel), tr(STR_SERIES_PAGE_RANGE_FORMAT), buffer);
      text(Rect{s.x + seriesScale(16), footerY, s.width / 2, smallLine()}, pageLabel, false, SERIES_CAPTION_FONT_ID);
    } else
      text(Rect{s.x + seriesScale(16), footerY, s.width / 2, smallLine()},
           CROSSINK_APP_CAP_TOUCH ? tr(STR_SERIES_TAP_CHOOSE) : tr(STR_SERIES_BUTTON_HINT), false,
           SERIES_CAPTION_FONT_ID);
    const char* context = tr(STR_SERIES_CONTEXT);
    const int contextWidth = renderer.getTextWidth(SERIES_CAPTION_FONT_ID, context);
    text(Rect{s.x + s.width - seriesScale(16) - contextWidth, footerY, contextWidth, smallLine()}, context, false,
         SERIES_CAPTION_FONT_ID);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    painted_ = true;
  }
};
static_assert(sizeof(SeriesActivity) <= 56 * 1024);
}  // namespace
void showSeries(GfxRenderer& renderer, MappedInputManager& input, Library& library, const char* key, BookKey current,
                bool known) {
  // The bounded cached windows/flow/cover workspace is too large for a task
  // stack. Allocate once for this foreground lifetime and free on pop.
  auto screen = makeUniqueNoThrow<SeriesActivity>(renderer, input, library, key, current, known);
  if (!screen) {
    LOG_ERR("BSS", "Cannot allocate Series screen");
    return;
  }
  activityManager.pushActivity(std::move(screen));
}
void drawSeriesGroupHeader(GfxRenderer& renderer, int x, int y, int width, const char* title, uint32_t total) {
  char label[48];
  std::snprintf(label, sizeof(label), tr(STR_SERIES_ALL_FORMAT), static_cast<unsigned long>(total));
  const int labelWidth = renderer.getTextWidth(UI_10_FONT_ID, label, EpdFontFamily::BOLD);
  const int target = std::min(width / 2, labelWidth + 16);
  renderer.beginTextClip(x, y, width - target, 50);
  renderer.drawText(UI_10_FONT_ID, x, y, title, true, EpdFontFamily::BOLD);
  renderer.endTextClip();
  renderer.beginTextClip(x + width - target, y, target, 50);
  renderer.drawText(UI_10_FONT_ID, x + width - labelWidth, y, label, true, EpdFontFamily::BOLD);
  renderer.endTextClip();
}
#ifdef SIMULATOR
bool seriesScenarioInvariant() { return !scenarioActivity || scenarioActivity->scenarioInvariant(); }
void seriesScenarioGeometry(const char* path) {
  FILE* file = std::fopen(path, "wb");
  if (!file) {
    LOG_ERR("BSS", "Cannot retain Series geometry");
    return;
  }
  std::fputs("[", file);
  for (unsigned i = 0; i < textBoxCount_; ++i) {
    const auto& box = textBoxes_[i];
    std::fprintf(file, "%s{\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d,\"layer\":%u,\"text\":\"", i ? "," : "",
                 box.bounds.x, box.bounds.y, box.bounds.width, box.bounds.height, box.layer);
    for (const unsigned char* byte = reinterpret_cast<const unsigned char*>(box.label); *byte; ++byte) {
      if (*byte == '"' || *byte == '\\') std::fputc('\\', file);
      if (*byte < 32)
        std::fprintf(file, "\\u%04x", *byte);
      else
        std::fputc(*byte, file);
    }
    std::fputs("\"}", file);
  }
  std::fputs("]\n", file);
  if (std::fclose(file)) LOG_ERR("BSS", "Cannot close Series geometry evidence");
}

void seriesScenarioBegin(bool count, const char* typed) {
  RenderLock lock;
  if (scenarioActivity) scenarioActivity->scenarioBegin(count, typed);
}
uint32_t seriesPaints() {
  RenderLock lock;
  return paints;
}
#endif
}  // namespace booksync

extern "C" void booksync_series_screen_link(GfxRenderer& renderer, MappedInputManager& input,
                                            booksync::Library& library, const char* key) {
  booksync::showSeries(renderer, input, library, key);
}
