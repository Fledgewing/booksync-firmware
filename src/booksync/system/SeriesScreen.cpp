// SPDX-License-Identifier: GPL-3.0-or-later
#include "SeriesScreen.h"

#include <AppCapabilities.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

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
#endif
class SeriesActivity final : public Activity {
  Library& library_;
  Series series_;
  SeriesRequest initial_{};
  uint8_t focus_ = 0, scroll_ = 0;
  uint32_t lastPoll_ = 0;
  bool painted_ = false;
#if !CROSSINK_APP_CAP_HOME_EDITOR_SETTINGS
  // Three prepared 48x72 covers plus one reusable F25 bitmap. No second framebuffer.
  std::array<std::array<uint8_t, 432>, 3> covers_{};
  std::array<CoverReference, 3> references_{};
  std::array<BookKey, 3> keys_{};
  CoverBitmap bitmap_{};
#endif
  Rect safe() const { return UITheme::getInstance().getScreenSafeArea(renderer, true, false); }
  // Normalize approved design coordinates to the current safe viewport; these
  // reference units are not physical panel dimensions.
  int seriesScale(int value) const {
    return std::max(1, std::min(value * safe().width / 480, value * safe().height / 800));
  }
  int textFont() const { return CROSSINK_APP_CAP_HOME_EDITOR_SETTINGS ? SMALL_FONT_ID : UI_10_FONT_ID; }
  int titleFont() const { return CROSSINK_APP_CAP_HOME_EDITOR_SETTINGS ? UI_10_FONT_ID : UI_12_FONT_ID; }
  int seriesTextLine() const { return renderer.getLineHeight(textFont()); }
  int seriesTop() const {
    const auto s = safe();
    return s.y + seriesScale(64);
  }
  int bottom() const {
    const auto s = safe();
    return s.y + s.height - seriesScale(50);
  }
  void text(Rect r, const char* value, bool bold = false, int font = 0) const {
    if (r.width < 1 || r.height < 1) return;
    renderer.beginTextClip(r.x, r.y, r.width, r.height);
    renderer.drawText(font ? font : textFont(), r.x, r.y, value, true,
                      bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    renderer.endTextClip();
  }
  void wrapped(Rect area, const char* value, uint8_t maxLines, bool bold = false, int font = 0) const {
    const int selectedFont = font ? font : textFont();
    const int height = renderer.getLineHeight(selectedFont);
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
    renderer.drawRoundedRect(r.x, r.y, r.width, r.height, thick, seriesScale(10), true);
  }
  Rect pageRow(uint8_t i) const {
    const auto s = safe();
    return Rect{s.x + seriesScale(16), seriesTop() + seriesScale(336) + i * seriesScale(91), s.width - seriesScale(32),
                seriesScale(91)};
  }
  Rect downloadButton() const {
    const auto s = safe();
    return Rect{s.x + seriesScale(28), seriesTop() + seriesScale(258), s.width - seriesScale(56), seriesScale(50)};
  }
  Rect seriesPanel() const {
    const auto s = safe();
    const int available = bottom() - seriesTop() - seriesScale(20);
    int needed = sheetListTop() + rowCount() * rowHeight() + actionCount() * seriesScale(54) + seriesScale(18);
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
    if (series_.scene() == SeriesScene::Count) needed = seriesScale(CROSSINK_APP_CAP_TOUCH ? 480 : 300);
    const int height = std::min(available, needed);
    const int y = series_.scene() == SeriesScene::Count && CROSSINK_APP_CAP_TOUCH
                      ? bottom() - seriesScale(10) - height
                      : seriesTop() + seriesScale(10) + (available - height) / 2;
    return Rect{s.x + seriesScale(16), y, s.width - seriesScale(32), height};
  }
  Rect keyboard() const {
    const auto s = safe();
    return Rect{s.x + seriesScale(16), bottom() - seriesScale(224), s.width - seriesScale(32), seriesScale(216)};
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
    return std::max(seriesScale(50),
                    seriesTextLine() * (series_.scene() == SeriesScene::Review ? 3 : 2) + seriesScale(8));
  }
  int sheetListTop() const { return series_.scene() == SeriesScene::Queue ? seriesScale(136) : seriesScale(76); }
  int visibleRows() const {
    const auto r = seriesPanel();
    const int available = r.height - sheetListTop() - seriesScale(6) - actionCount() * seriesScale(54);
    return std::max(1, available / rowHeight());
  }
  Rect sheetRow(uint8_t i) const {
    const auto r = seriesPanel();
    return Rect{r.x + seriesScale(12), r.y + sheetListTop() + (int(i) - scroll_) * rowHeight(),
                r.width - seriesScale(24), rowHeight()};
  }
  Rect actionRect(uint8_t i) const {
    const auto r = seriesPanel();
    if (series_.scene() == SeriesScene::Count) {
#if CROSSINK_APP_CAP_TOUCH
      const auto k = keyboard();
      return Rect{r.x + seriesScale(12) + i * (r.width - seriesScale(24)) / 2, k.y - seriesScale(62),
                  (r.width - seriesScale(32)) / 2, seriesScale(50)};
#else
      return Rect{r.x + seriesScale(12) + i * (r.width - seriesScale(24)) / 2, r.y + r.height - seriesScale(66),
                  (r.width - seriesScale(32)) / 2, seriesScale(50)};
#endif
    }
    return Rect{r.x + seriesScale(12), r.y + r.height - seriesScale(12) - (actionCount() - i) * seriesScale(54),
                r.width - seriesScale(24), seriesScale(50)};
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
  void sizeText(char* out, size_t capacity, uint64_t bytes, bool known) const {
    if (known)
      std::snprintf(out, capacity, tr(STR_SERIES_BYTES_FORMAT), static_cast<unsigned long long>(bytes));
    else
      std::snprintf(out, capacity, "%s", tr(STR_SERIES_SIZE_UNKNOWN));
  }
  void header() const {
    const auto s = safe();
    text(Rect{s.x + seriesScale(12), s.y + seriesScale(14), seriesScale(90), seriesScale(40)}, tr(STR_BACK));
    text(Rect{s.x + seriesScale(108), s.y + seriesScale(9), s.width - seriesScale(176), seriesScale(44)},
         tr(STR_SERIES_TITLE), true, titleFont());
    // Search is a top-right icon with a 50-design-pixel target, not a new route.
    const int x = s.x + s.width - seriesScale(46), y = s.y + seriesScale(16), z = seriesScale(17);
    renderer.drawRoundedRect(x, y, z, z, 2, z / 2, true);
    renderer.drawLine(x + z, y + z, x + z + seriesScale(9), y + z + seriesScale(9), 2, true);
    renderer.drawLine(s.x, s.y + seriesScale(58), s.x + s.width, s.y + seriesScale(58));
  }
  void totals(int y) const {
    const auto s = safe();
    const auto& plan = series_.plan();
    const uint32_t values[] = {plan.total, plan.read, plan.reading};
    const char* labels[] = {tr(STR_SERIES_BOOKS), tr(STR_SERIES_READ), tr(STR_SERIES_READING)};
    for (uint8_t i = 0; i < 3; ++i) {
      const int x = s.x + seriesScale(16) + i * (s.width - seriesScale(32)) / 3;
      char n[16];
      std::snprintf(n, sizeof(n), "%lu", static_cast<unsigned long>(values[i]));
      const int width = (s.width - seriesScale(32)) / 3;
      int valueFont = UI_12_FONT_ID;
      if (renderer.getTextWidth(valueFont, n, EpdFontFamily::BOLD) > width) valueFont = UI_10_FONT_ID;
      const int valueWidth = renderer.getTextWidth(valueFont, n, EpdFontFamily::BOLD);
      const bool stacked = CROSSINK_APP_CAP_HOME_EDITOR_SETTINGS ||
                           valueWidth + seriesScale(6) + renderer.getTextWidth(SMALL_FONT_ID, labels[i]) > width;
      text(Rect{x, y, width, seriesScale(42)}, n, true, valueFont);
      text(Rect{x + (stacked ? 0 : valueWidth + seriesScale(6)), y + seriesScale(stacked ? 32 : 8),
                width - (stacked ? 0 : valueWidth + seriesScale(6)), seriesScale(24)},
           labels[i], false, SMALL_FONT_ID);
    }
    if (plan.availability == Availability::Ready && plan.total) {
      const unsigned count = std::min<uint32_t>(plan.total, 24);
      const int width = (s.width - seriesScale(32)) / count;
      for (unsigned i = 0; i < count; ++i) {
        const int x = s.x + seriesScale(16) + i * width;
        renderer.drawRoundedRect(x, y + seriesScale(48), width - seriesScale(3), seriesScale(7), 1, seriesScale(3),
                                 true);
        if (i < plan.read)
          renderer.fillRect(x + 1, y + seriesScale(48) + 1, width - seriesScale(3) - 2, seriesScale(7) - 2);
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
        const Rect retry{s.x + seriesScale(16), seriesTop() + seriesScale(98), seriesScale(140), seriesScale(50)};
        outline(retry);
        text(Rect{retry.x + seriesScale(12), retry.y + seriesScale(10), retry.width - seriesScale(24),
                  retry.height - seriesScale(12)},
             tr(STR_RETRY), true);
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
         subtitle, false, SMALL_FONT_ID);
    if (plan.countsKnown || plan.availability == Availability::Ready) totals(seriesTop() + seriesScale(70));
    const Rect card{s.x + seriesScale(16), seriesTop() + seriesScale(140), s.width - seriesScale(32), seriesScale(176)};
    outline(card);
    char title[96];
    if (plan.count)
      std::snprintf(title, sizeof(title), tr(STR_SERIES_NEXT_TITLE_FORMAT), unsigned(plan.count));
    else
      std::snprintf(title, sizeof(title), "%s", tr(STR_SERIES_QUEUE));
    text(Rect{card.x + seriesScale(12), card.y + seriesScale(10), card.width - seriesScale(60), seriesScale(30)}, title,
         true);
    // The approved card's download icon stays at its top right.
    const int ix = card.x + card.width - seriesScale(30), iy = card.y + seriesScale(12);
    renderer.drawLine(ix, iy, ix, iy + seriesScale(18), 2, true);
    renderer.drawLine(ix - seriesScale(7), iy + seriesScale(11), ix, iy + seriesScale(18));
    renderer.drawLine(ix, iy + seriesScale(18), ix + seriesScale(7), iy + seriesScale(11));
    const char* summary = plan.membershipIncomplete ? tr(STR_SERIES_MEMBERSHIP_INCOMPLETE)
                          : !plan.deviceKnown       ? tr(STR_SERIES_DEVICE_UNKNOWN)
                          : plan.count              ? tr(STR_SERIES_KEEP_READY)
                          : plan.earlierKnown       ? tr(STR_SERIES_EARLIER_PENDING)
                          : plan.remainingSaved     ? tr(STR_SERIES_REMAINING_SAVED)
                                                    : tr(STR_SERIES_NO_REMAINING);
    text(Rect{card.x + seriesScale(12), card.y + seriesScale(43), card.width - seriesScale(24), seriesScale(24)},
         summary, false, SMALL_FONT_ID);
    for (uint8_t i = 0; i < std::min<uint8_t>(plan.count, 3); ++i) {
      if (plan.next[i].positionKnown) {
        char number[24];
        std::snprintf(number, sizeof(number), "%.6g", plan.next[i].position);
        std::snprintf(title, sizeof(title), tr(STR_SERIES_PAIR_FORMAT), number, plan.next[i].book.title.data());
      } else
        std::snprintf(title, sizeof(title), "%s", plan.next[i].book.title.data());
      text(Rect{card.x + seriesScale(12), card.y + seriesScale(66) + i * seriesScale(18), card.width - seriesScale(24),
                seriesScale(18)},
           title, false, SMALL_FONT_ID);
    }
    const auto d = downloadButton();
    outline(d, !CROSSINK_APP_CAP_TOUCH && focus_ == 0 ? 2 : 1);
    text(Rect{d.x + seriesScale(12), d.y + seriesScale(10), d.width - seriesScale(24), d.height - seriesScale(12)},
         plan.membershipIncomplete    ? tr(STR_SERIES_MEMBERSHIP_INCOMPLETE)
         : !series_.downloadEnabled() ? tr(STR_SERIES_UNAVAILABLE_SOURCE)
         : plan.count                 ? tr(STR_SERIES_DOWNLOAD_NEXT)
                                      : tr(STR_SERIES_DOWNLOADS_MORE),
         true);
    text(Rect{s.x + seriesScale(16), seriesTop() + seriesScale(318), seriesScale(130), seriesScale(24)},
         tr(STR_SERIES_MEMBERS), true);
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
      text(Rect{r.x, r.y + seriesScale(30), seriesScale(30), seriesScale(30)}, number);
      int left = r.x + seriesScale(34);
#if !CROSSINK_APP_CAP_HOME_EDITOR_SETTINGS
      const Rect c{left, r.y + seriesScale(9), seriesScale(48), seriesScale(72)};
      if (references_[i].id) {
        for (int y = 0; y < c.height; ++y)
          for (int x = 0; x < c.width; ++x) {
            const unsigned sx = c.width > 1 ? x * 47 / (c.width - 1) : 0,
                           sy = c.height > 1 ? y * 71 / (c.height - 1) : 0;
            if (!(covers_[i][sy * 6 + sx / 8] & (0x80 >> (sx % 8)))) renderer.drawPixel(c.x + x, c.y + y);
          }
      } else
        renderer.drawRect(c.x, c.y, c.width, c.height);
      left += seriesScale(58);
#endif
      const int width = r.x + r.width - left - seriesScale(6);
      text(Rect{left, r.y + seriesScale(10), width, seriesScale(30)}, row.book.title.data(), true);
      text(Rect{left, r.y + seriesScale(39), width, seriesScale(22)}, row.label.data(), false, SMALL_FONT_ID);
      const char* status = row.status == QueryShow::Finished ? tr(STR_SERIES_READ_STATUS)
                           : (row.status == QueryShow::Reading || row.status == QueryShow::Rereading)
                               ? tr(STR_SERIES_READING)
                           : row.onDevice == QueryTruth::Yes ? tr(STR_SERIES_ON_DEVICE)
                           : row.onDevice == QueryTruth::No  ? tr(STR_SERIES_ON_SERVER)
                                                             : tr(STR_SERIES_UNKNOWN);
      const bool reading = row.status == QueryShow::Reading || row.status == QueryShow::Rereading;
      if (reading && row.book.readingKnown && row.book.pages) {
        const auto percentage = std::min<uint64_t>(100, uint64_t(row.book.page) * 100 / row.book.pages);
        std::snprintf(title, sizeof(title), tr(STR_SERIES_PROGRESS_FORMAT), unsigned(percentage),
                      tr(STR_SERIES_READING));
        text(Rect{left, r.y + seriesScale(63), width, seriesScale(22)}, title, false, SMALL_FONT_ID);
#if !CROSSINK_APP_CAP_HOME_EDITOR_SETTINGS
        const int bar = std::min(width, seriesScale(110)), height = seriesScale(4);
        renderer.drawRect(left, r.y + seriesScale(84), bar, height);
        if (percentage && bar > 2 && height > 2)
          renderer.fillRect(left + 1, r.y + seriesScale(84) + 1, (bar - 2) * percentage / 100, height - 2);
#endif
      } else
        text(Rect{left, r.y + seriesScale(63), width, seriesScale(22)}, status, false, SMALL_FONT_ID);
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
    outline(r, 2);
    char buffer[160];
    const auto& f = series_.flow();
    const char* title = tr(STR_SERIES_QUEUE);
    switch (series_.scene()) {
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
    // Compact top-right download icon on existing dialog, no extra control.
    renderer.drawLine(r.x + r.width - seriesScale(24), r.y + seriesScale(14), r.x + r.width - seriesScale(24),
                      r.y + seriesScale(32), 2, true);
    renderer.drawLine(r.x + r.width - seriesScale(30), r.y + seriesScale(26), r.x + r.width - seriesScale(24),
                      r.y + seriesScale(32));
    renderer.drawLine(r.x + r.width - seriesScale(24), r.y + seriesScale(32), r.x + r.width - seriesScale(18),
                      r.y + seriesScale(26));
    if (series_.scene() == SeriesScene::Count) {
      const Rect field{r.x + seriesScale(16), r.y + seriesScale(90), r.width - seriesScale(32), seriesScale(80)};
      outline(field);
      text(Rect{field.x + seriesScale(16), field.y + seriesScale(12), field.width - seriesScale(32), seriesScale(54)},
           series_.countText(), true, UI_12_FONT_ID);
#if CROSSINK_APP_CAP_TOUCH
      static const char* digits[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", nullptr, "0", nullptr};
      for (uint8_t i = 0; i < 12; ++i) {
        const auto k = keyRect(i);
        outline(k);
        if (i == 9) {
          const int x = k.x + k.width / 2 - seriesScale(10), y = k.y + k.height / 2;
          renderer.drawLine(x - seriesScale(8), y, x, y - seriesScale(9));
          renderer.drawLine(x - seriesScale(8), y, x, y + seriesScale(9));
          renderer.drawLine(x, y - seriesScale(9), x + seriesScale(22), y - seriesScale(9));
          renderer.drawLine(x, y + seriesScale(9), x + seriesScale(22), y + seriesScale(9));
          renderer.drawLine(x + seriesScale(22), y - seriesScale(9), x + seriesScale(22), y + seriesScale(9));
          renderer.drawLine(x + seriesScale(5), y - seriesScale(5), x + seriesScale(15), y + seriesScale(5));
          renderer.drawLine(x + seriesScale(15), y - seriesScale(5), x + seriesScale(5), y + seriesScale(5));
        } else
          text(
              Rect{k.x + seriesScale(12), k.y + seriesScale(10), k.width - seriesScale(20), k.height - seriesScale(12)},
              digits[i] ? digits[i] : tr(STR_SERIES_REVIEW), true);
      }
#else
      text(Rect{r.x + seriesScale(16), field.y + field.height + seriesScale(16), r.width - seriesScale(32),
                seriesScale(40)},
           tr(STR_SERIES_NUMBER_LABEL));
#endif
    }
    if (series_.scene() == SeriesScene::Queue) {
      uint8_t counts[3]{};
      for (uint8_t i = 0; i < f.queue.count; ++i) {
        const auto state = f.queue.items[i].state;
        if (state == SeriesItemState::Complete)
          ++counts[0];
        else if (state == SeriesItemState::Canceled)
          ++counts[1];
        else if (state == SeriesItemState::Failed || state == SeriesItemState::Unsupported)
          ++counts[2];
      }
#if CROSSINK_APP_CAP_HOME_EDITOR_SETTINGS
      std::snprintf(buffer, sizeof(buffer), tr(STR_SERIES_QUEUE_COUNTS_FORMAT), counts[0], tr(STR_SERIES_SAVED),
                    counts[1], tr(STR_SERIES_SKIPPED), counts[2], tr(STR_SERIES_FAILED));
      wrapped(Rect{r.x + seriesScale(12), r.y + seriesScale(48), r.width - seriesScale(24), seriesScale(80)}, buffer,
              3);
#else
      const char* labels[] = {tr(STR_SERIES_SAVED), tr(STR_SERIES_SKIPPED), tr(STR_SERIES_FAILED),
                              tr(STR_SERIES_REQUESTED)};
      for (uint8_t i = 0; i < 4; ++i) {
        const int width = (r.width - seriesScale(24)) / 4;
        const int x = r.x + seriesScale(12) + i * width;
        char number[8];
        std::snprintf(number, sizeof(number), "%u", i == 3 ? f.queue.count : counts[i]);
        text(Rect{x, r.y + seriesScale(48), width, seriesScale(42)}, number, true, UI_12_FONT_ID);
        text(Rect{x, r.y + seriesScale(91), width, seriesScale(24)}, labels[i], false, SMALL_FONT_ID);
      }
#endif
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
      if (focus_ == i) outline(row, 2);
      text(Rect{row.x + seriesScale(6), row.y + seriesScale(4), row.width - seriesScale(12),
                seriesTextLine() + seriesScale(4)},
           titleText, true);
      if (series_.scene() == SeriesScene::Preview)
        sizeText(buffer, sizeof(buffer), f.review[i].bytes, f.review[i].sizeKnown);
      else if (series_.scene() == SeriesScene::Files)
        sizeText(buffer, sizeof(buffer), f.files[i].bytes, f.files[i].sizeKnown);
      else if (series_.scene() == SeriesScene::Review) {
        if (!f.review[i].supported)
          std::snprintf(buffer, sizeof(buffer), "%s", tr(STR_SERIES_NO_FILE));
        else {
          char bytes[40];
          sizeText(bytes, sizeof(bytes), f.review[i].bytes, f.review[i].sizeKnown);
          std::snprintf(buffer, sizeof(buffer), tr(STR_SERIES_PAIR_FORMAT), f.review[i].label.data(), bytes);
        }
      } else {
        const auto& item = f.queue.items[i];
        const auto* state = stateLabel(item.state);
        if (item.job && item.state != SeriesItemState::Complete)
          std::snprintf(buffer, sizeof(buffer), tr(STR_SERIES_CANCEL_SUFFIX_FORMAT), state);
        else
          std::snprintf(buffer, sizeof(buffer), "%s", state);
      }
      text(Rect{row.x + seriesScale(6), row.y + seriesTextLine() + seriesScale(6), row.width - seriesScale(12),
                seriesTextLine() + seriesScale(4)},
           buffer);
      if (series_.scene() == SeriesScene::Review && f.review[i].supported)
        text(Rect{row.x + seriesScale(6), row.y + 2 * seriesTextLine() + seriesScale(8), row.width - seriesScale(12),
                  seriesTextLine()},
             f.review[i].destination.data());
      renderer.drawLine(row.x, row.y + row.height - seriesScale(2), row.x + row.width,
                        row.y + row.height - seriesScale(2));
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
      outline(a, focus_ == rows + i ? 2 : 1);
      text(Rect{a.x + seriesScale(10), a.y + seriesScale(10), a.width - seriesScale(20), a.height - seriesScale(12)},
           actionLabel(i, buffer, sizeof(buffer)), true);
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
    LOG_INF("BSS", "Series screen workspace=%u bytes", unsigned(sizeof(SeriesActivity)));
    series_.enterSeries(initial_.key.data(), initial_.current, initial_.currentKnown);
    series_.pollSeries();
    focus_ = series_.members().count ? 1 : 0;
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
        (tapped && inside(Rect{s.x, s.y, seriesScale(100), seriesScale(58)}, x, y))) {
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
      const unsigned controls =
          series_.scene() == SeriesScene::Page ? series_.members().count + 2 : rowCount() + actionCount();
      if (mappedInput.wasReleased(mappedInput.menuButton(MappedInputManager::Button::Down))) {
        if (focus_ + 1 < controls) ++focus_;
        changed = true;
      }
      if (mappedInput.wasReleased(mappedInput.menuButton(MappedInputManager::Button::Up))) {
        if (focus_) --focus_;
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
          if (inside(Rect{s.x + s.width - seriesScale(58), s.y, seriesScale(58), seriesScale(58)}, x, y))
            series_.searchSeries();
          if (inside(downloadButton(), x, y)) activate(0);
          if (inside(Rect{s.x, bottom(), seriesScale(80), seriesScale(50)}, x, y)) series_.nextSeriesPage(-1);
          if (inside(Rect{s.x + s.width - seriesScale(80), bottom(), seriesScale(80), seriesScale(50)}, x, y))
            series_.nextSeriesPage(1);
          if (series_.retryEnabled() &&
              inside(Rect{s.x + seriesScale(16), seriesTop() + seriesScale(98), seriesScale(140), seriesScale(50)}, x,
                     y))
            series_.retrySeriesPage();
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
      if (focus_ < rowCount()) {
        if (focus_ < scroll_) scroll_ = focus_;
        if (focus_ >= scroll_ + visibleRows()) scroll_ = focus_ - visibleRows() + 1;
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
    if (previous != series_.scene()) {
      focus_ = series_.scene() == SeriesScene::Page ? (series_.members().count ? 1 : 0) : rowCount();
      scroll_ = 0;
      changed = true;
    }
    if (!series_.live())
      activityManager.popActivity();
    else if (changed)
      requestUpdate();
  }
#ifdef SIMULATOR
  bool scenarioInvariant() const {
    auto ink = [&](Rect r) {
      for (int y = r.y; y < r.y + r.height; ++y)
        for (int x = r.x; x < r.x + r.width; ++x)
          if (renderer.isPixelBlack(x, y)) return true;
      return false;
    };
    if (series_.scene() == SeriesScene::Count) {
      const auto r = seriesPanel();
      if (!ink(Rect{r.x + seriesScale(32), r.y + seriesScale(102), seriesScale(70), seriesScale(54)})) return false;
    }
#if !CROSSINK_APP_CAP_HOME_EDITOR_SETTINGS
    if (series_.scene() == SeriesScene::Page && series_.members().count) {
      for (uint8_t i = 0; i < std::min<uint16_t>(series_.members().count, 3); ++i)
        if (references_[i].id) {
          const auto r = pageRow(i);
          const int x = r.x + seriesScale(34) + seriesScale(48) - 1, y = r.y + seriesScale(9), height = seriesScale(72);
          unsigned edge = 0;
          for (int yy = y; yy < y + height; ++yy)
            if (renderer.isPixelBlack(x, yy)) ++edge;
          if (edge < unsigned(height / 2)) return false;
        }
    }
    if (series_.scene() == SeriesScene::Queue) {
      const auto r = seriesPanel();
      const int width = (r.width - seriesScale(24)) / 4;
      for (uint8_t i = 0; i < 4; ++i)
        if (!ink(Rect{r.x + seriesScale(12) + i * width, r.y + seriesScale(48), width, seriesScale(36)})) return false;
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
    focus_ = rowCount();
    scroll_ = 0;
    requestUpdate();
  }
#endif
  void render(RenderLock&&) override {
    if (!series_.live()) return;
#ifdef SIMULATOR
    ++paints;
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
          if ((x & 1) || (y & 1)) renderer.drawPixel(x, y, false);
      sheet();
    }
    const auto s = safe();
    renderer.drawLine(s.x, bottom(), s.x + s.width, bottom());
    char buffer[72];
    const auto first = series_.members().count ? series_.offset() + 1 : 0;
    std::snprintf(buffer, sizeof(buffer), tr(STR_SERIES_RANGE_FORMAT), static_cast<unsigned long>(first),
                  static_cast<unsigned long>(series_.offset() + series_.members().count),
                  static_cast<unsigned long>(series_.members().matchedBooks));
    if(series_.members().availability==Availability::Ready && series_.members().count)
      text(Rect{s.x+seriesScale(70),bottom()+seriesScale(8),s.width-seriesScale(140),seriesScale(30)},buffer);
    else text(Rect{s.x+seriesScale(16),bottom()+seriesScale(8),s.width-seriesScale(32),seriesScale(30)},tr(STR_BACK));
    if (series_.scene() == SeriesScene::Page) {
      const int y = bottom() + seriesScale(20);
      if (series_.offset()) {
        const int x = s.x + seriesScale(24);
        renderer.drawLine(x + seriesScale(8), y - seriesScale(8), x, y);
        renderer.drawLine(x, y, x + seriesScale(8), y + seriesScale(8));
      }
      if (series_.members().hasMore) {
        const int x = s.x + s.width - seriesScale(24);
        renderer.drawLine(x - seriesScale(8), y - seriesScale(8), x, y);
        renderer.drawLine(x, y, x - seriesScale(8), y + seriesScale(8));
      }
    }
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
  const int target = std::min(width / 2, std::max(100, width * 145 / 448));
  char label[48];
  std::snprintf(label, sizeof(label), tr(STR_SERIES_ALL_FORMAT), static_cast<unsigned long>(total));
  renderer.beginTextClip(x, y, width - target, 50);
  renderer.drawText(UI_10_FONT_ID, x, y, title, true, EpdFontFamily::BOLD);
  renderer.endTextClip();
  renderer.beginTextClip(x + width - target, y, target, 50);
  renderer.drawText(UI_10_FONT_ID, x + width - target, y, label, true, EpdFontFamily::BOLD);
  renderer.endTextClip();
}
#ifdef SIMULATOR
bool seriesScenarioInvariant() { return !scenarioActivity || scenarioActivity->scenarioInvariant(); }
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
