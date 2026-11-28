// SPDX-License-Identifier: GPL-3.0-or-later
#include "CatalogScreen.h"

#include <AppCapabilities.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdio>

#include "AccountScreen.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "booksync/services/LocalLibrary.h"
#include "booksync/system/NetworkPhase.h"
#include "booksync/ui/Catalog.h"
#include "components/UITheme.h"
#include "fontIds.h"
namespace booksync {
namespace {
#ifdef SIMULATOR
uint32_t paints = 0;
CatalogSurface* sceneController = nullptr;
struct CatalogTextGeometry {
  Rect ink{}, slot{}, container{};
  std::array<char, 160> label{};
  uint8_t layer = 0;
};
// Host-only diagnostic storage. It is absent from every device build.
std::array<CatalogTextGeometry, 256> catalogGeometry{};
unsigned catalogGeometryCount = 0;
uint8_t geometryLayer = 0;
void recordCatalogText(Rect ink, Rect slot, Rect container, const char* label, uint8_t layer) {
  if (catalogGeometryCount >= catalogGeometry.size()) return;
  auto& value = catalogGeometry[catalogGeometryCount++];
  value.ink = ink;
  value.slot = slot;
  value.container = container;
  value.layer = layer;
  std::snprintf(value.label.data(), value.label.size(), "%s", label);
}

#endif
const char* rootTitle(CatalogRoot root) {
  switch (root) {
    case CatalogRoot::Authors:
      return tr(STR_HOME_AUTHORS);
    case CatalogRoot::Series:
      return tr(STR_HOME_SERIES);
    case CatalogRoot::OnCard:
      return tr(STR_CATALOG_ON_CARD);
    case CatalogRoot::Lists:
      return tr(STR_CATALOG_LISTS);
    case CatalogRoot::Libraries:
      return tr(STR_CATALOG_LIBRARIES);
    case CatalogRoot::Collections:
      return tr(STR_CATALOG_COLLECTIONS);
    case CatalogRoot::Reading:
      return tr(STR_CATALOG_READING);
    case CatalogRoot::Recent:
      return tr(STR_CATALOG_RECENT);
    default:
      return tr(STR_CATALOG_ALL);
  }
}
const char* stateTitle(CatalogSurface::State state, bool sources = false) {
  switch (state) {
    case CatalogSurface::State::Loading:
      return tr(STR_HOME_LOADING);
    case CatalogSurface::State::NotSaved:
      return tr(STR_HOME_NOT_SAVED);
    case CatalogSurface::State::Unsupported:
      return tr(STR_HOME_UNAVAILABLE);
    case CatalogSurface::State::Error:
      return tr(STR_HOME_WIDGET_ERROR);
    case CatalogSurface::State::Empty:
      return sources ? tr(STR_CATALOG_NO_LISTS) : tr(STR_CATALOG_EMPTY);
    default:
      return "";
  }
}
const char* errorTitle(DownloadError error) {
  switch (error) {
    case DownloadError::NoSpace:
      return tr(STR_CATALOG_SPACE);
    case DownloadError::Edition:
    case DownloadError::Digest:
    case DownloadError::Invalid:
      return tr(STR_CATALOG_INVALID_FILE);
    case DownloadError::Unavailable:
      return tr(STR_CATALOG_UNSUPPORTED_FILE);
    default:
      return tr(STR_DOWNLOAD_FAILED);
  }
}
class CatalogActivity final : public Activity {
  Library& library_;
  std::unique_ptr<CatalogSurface> catalog_;
  // S3-only optional decoded thumbnails: <=24.1 KiB, once per foreground
  // activity, too large for the task stack. X3 never allocates this workspace.
  std::unique_ptr<CoverBitmap[]> bitmaps_;
  std::array<CoverReference, 8> loaded_{};
  struct Hit {
    Rect box{};
    uint8_t index = 0, action = 0;
  };
  mutable std::array<Hit, 32> hits_{};
  mutable uint8_t hitCount_ = 0;
  mutable Rect textContainer_{};
  // Reused paint scratch in the existing fallible Activity allocation (304 B).
  // Keeping it off the C3 render-task stack avoids large nested formatting frames.
  mutable char label_[160]{}, detail_[96]{}, count_[24]{}, total_[24]{};
  bool ownsLocal_ = false, textOnly_ = true, longConfirmHandled_ = false;
  Rect safe() const { return UITheme::getInstance().getScreenSafeArea(renderer, false, false); }
  int catalogPixels(int value) const {
    const auto bounds = safe();
    const int scale = std::min({renderer.getScreenWidth() * 256 / 480, renderer.getScreenHeight() * 256 / 800,
                                bounds.width * 256 / 448, bounds.height * 256 / 772});
    return (value * scale + 128) / 256;
  }
  Rect catalogRect(int x, int y, int width, int height) const {
    const auto s = safe();
    const int originX = std::max(s.x - catalogPixels(16), std::min((renderer.getScreenWidth() - catalogPixels(480)) / 2,
                                                                   s.x + s.width - catalogPixels(464)));
    const int originY =
        std::max(s.y - catalogPixels(14),
                 std::min((renderer.getScreenHeight() - catalogPixels(800)) / 2, s.y + s.height - catalogPixels(786)));
    return Rect{originX + catalogPixels(x), originY + catalogPixels(y), catalogPixels(width), catalogPixels(height)};
  }
  bool catalogSymbol(uint32_t cp) const { return cp == 0x22ef || cp == 0x25cf || cp == 0x25d0 || cp == 0x25cb; }
  int catalogSymbolWidth(uint32_t cp, int pixels) const { return cp == 0x22ef ? pixels : pixels * 3 / 4 + 2; }
  int paintCatalogSymbol(uint32_t cp, int x, int baseline, int pixels) const {
    const int advance = catalogSymbolWidth(cp, pixels);
    if (cp == 0x22ef) {
      for (int i = 0; i < 3; ++i) renderer.fillRect(x + pixels * (2 + i * 3) / 10, baseline - pixels / 4, 3, 3, true);
    } else {
      const int radius = std::max(2, (advance - 3) / 2), top = baseline - radius * 2;
      for (int y = 0; y <= radius * 2; ++y)
        for (int a = 0; a <= radius * 2; ++a) {
          const int dx = a - radius, dy = y - radius, square = dx * dx + dy * dy;
          if (square <= radius * radius &&
              (cp == 0x25cf || square >= (radius - 1) * (radius - 1) || (cp == 0x25d0 && dx <= 0)))
            renderer.drawPixel(x + a, top + y);
        }
    }
    return advance;
  }
  int headerBottom() const { return catalogRect(0, 54, 0, 0).y; }
  bool catalogFocused(uint8_t index) const { return !mappedInput.hasTouch() && catalog_->catalogFocus() == index; }
  void text(Rect r, const char* label, int size = 18, bool bold = false, int align = 0) const {
    if (!label || !*label || r.width <= 0 || r.height <= 0) return;
    const auto style = bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const int pixels = catalogPixels(size), scale = pixels * 256 / 22;
    const int line = (pixels * (size >= 29 ? 11 : 13) + 9) / 10;
    const auto* cursor = reinterpret_cast<const unsigned char*>(label);
    int y = r.y;
    while (*cursor) {
      char part[160];
      unsigned length = 0, lastSpace = 0;
      const auto* start = cursor;
      int width = 0, spaceWidth = 0;
      while (*cursor && *cursor != '\n' && length + 5 < sizeof(part)) {
        const auto* before = cursor;
        const auto cp = utf8NextCodepoint(&cursor);
        int32_t advance = 0;
        int top = 0;
        renderer.getCodepointMetrics(UI_10_FONT_ID, cp, style, advance, top);
        const int next = catalogSymbol(cp) ? catalogSymbolWidth(cp, pixels) : (advance * scale / 256 + 8) / 16;
        if (width + next > r.width && length) {
          cursor = before;
          break;
        }
        if (cp == ' ') {
          lastSpace = length;
          spaceWidth = width;
        }
        while (before != cursor) part[length++] = char(*before++);
        width += next;
      }
      if (*cursor && *cursor != '\n' && lastSpace) {
        cursor = start + lastSpace + 1;
        length = lastSpace;
        width = spaceWidth;
      }
      part[length] = 0;
      const int x = align == 1 ? r.x + (r.width - width) / 2 : align == 2 ? r.x + r.width - width : r.x;
      const int baseline = y + (line * 10 + pixels * 7 + 10) / 20;
#ifdef SIMULATOR
      const auto bounds = renderer.getTextVerticalBounds(UI_10_FONT_ID, part, style);
      const int ascender = renderer.getFontAscenderSize(UI_10_FONT_ID);
      const int inkTop = std::min(y, baseline + (bounds.top - ascender) * scale / 256);
      const int inkBottom = std::max(y + line, baseline + ((bounds.bottom - ascender) * scale + 255) / 256);
      recordCatalogText(Rect{x, inkTop, width, inkBottom - inkTop}, r, textContainer_, part, geometryLayer);
#endif
      if (y + line > r.y + r.height) break;
      const auto* glyph = reinterpret_cast<const unsigned char*>(part);
      int pen = x;
      while (*glyph) {
        const auto cp = utf8NextCodepoint(&glyph);
        pen += catalogSymbol(cp) ? paintCatalogSymbol(cp, pen, baseline, pixels)
                                 : renderer.drawScaledCodepoint(UI_10_FONT_ID, cp, style, pen, baseline, scale);
      }
      y += line;
      if (*cursor == '\n') ++cursor;
      while (*cursor == ' ') ++cursor;
    }
  }
  void hit(Rect r, uint8_t index, uint8_t action = 0) const {
    if (hitCount_ < hits_.size()) hits_[hitCount_++] = {r, index, action};
  }
  void button(Rect r, const char* label, uint8_t index, uint8_t action = 0) const {
    renderer.drawRect(r.x, r.y, r.width, r.height, catalogFocused(index) && !action ? 3 : 1, true);
    text(Rect{r.x + catalogPixels(12), r.y + catalogPixels(14), r.width - catalogPixels(24),
              r.height - catalogPixels(18)},
         label, 16, false, 1);
    hit(r, index, action);
  }
  void formatCatalogCount(char* value, size_t capacity, uint32_t number, bool commas = false) const {
    char digits[16];
    const int length = std::snprintf(digits, sizeof(digits), "%u", unsigned(number));
    size_t out = 0;
    for (int i = 0; i < length && out + 1 < capacity; ++i) {
      if (commas && i && (length - i) % 3 == 0 && out + 1 < capacity) value[out++] = ',';
      if (out + 1 < capacity) value[out++] = digits[i];
    }
    if (capacity) value[out] = 0;
  }
  void number(Rect r, CatalogCount count, bool all = false) const {
    char value[48];
    if (count.availability == Availability::Ready)
      std::snprintf(value, sizeof(value), all ? tr(STR_CATALOG_ALL_FORMAT) : "%u", unsigned(count.value));
    else
      std::snprintf(value, sizeof(value), "%s", all ? tr(STR_CATALOG_ALL_UNKNOWN) : "—");
    text(r, value, all ? 14 : textOnly_ ? 29 : 31, true, all ? 2 : 0);
  }
  void paintCatalogCover(Rect r, const BookRow& book, uint8_t index, unsigned placeholder = 0) const {
    if (r.width <= 2 || r.height <= 2) return;
    if (textOnly_) {
      text(r, book.title.data());
      return;
    }
    renderer.drawRect(r.x, r.y, r.width, r.height);
    const CoverBitmap* bitmap = bitmaps_ && index < loaded_.size() && loaded_[index].id ? &bitmaps_[index] : nullptr;
    if (!bitmap || !bitmap->width || !bitmap->height || bitmap->width > CoverWidth || bitmap->height > CoverHeight) {
      if (placeholder) {
        for (int y = r.y + 2; y < r.y + r.height - 2; y += 4)
          for (int x = r.x + 2; x < r.x + r.width - 2; x += 4) renderer.drawPixel(x, y);
        char value[16];
        std::snprintf(value, sizeof(value), "%u", placeholder);
        text(Rect{r.x + 1, r.y + (r.height - catalogPixels(24)) / 2, r.width - 2, catalogPixels(24)}, value, 16, false,
             1);
      } else
        text(Rect{r.x + 5, r.y + 6, r.width - 10, r.height - 12}, book.title.data(), 16);
      return;
    }
    // Scale the bounded cached 1-bit thumbnail directly into the sole renderer;
    // no decoder, path lookup, temporary framebuffer or allocation during paint.
    const int stride = (bitmap->width + 7) / 8;
    for (int y = 1; y + 1 < r.height; ++y) {
      const int sy = y * bitmap->height / r.height;
      for (int x = 1; x + 1 < r.width; ++x) {
        const int sx = x * bitmap->width / r.width;
        if (!(bitmap->pixels[sy * stride + sx / 8] & (0x80 >> (sx % 8)))) renderer.drawPixel(r.x + x, r.y + y);
      }
    }
  }
  void icon(Rect r, uint8_t kind) const {
    const int x = r.x + r.width - catalogPixels(33), y = r.y + catalogPixels(9);
    auto stroke = [&](int a, int b, int c, int d) {
      renderer.drawLine(x + catalogPixels((a * 22 + 12) / 24), y + catalogPixels((b * 22 + 12) / 24),
                        x + catalogPixels((c * 22 + 12) / 24), y + catalogPixels((d * 22 + 12) / 24));
    };
    if (kind == 0) {
      stroke(4, 4, 8, 4);
      stroke(8, 4, 8, 20);
      stroke(8, 20, 4, 20);
      stroke(4, 20, 4, 4);
      stroke(10, 3, 14, 3);
      stroke(14, 3, 14, 20);
      stroke(14, 20, 10, 20);
      stroke(10, 20, 10, 3);
      stroke(16, 5, 20, 4);
      stroke(20, 4, 23, 19);
      stroke(23, 19, 19, 20);
      stroke(19, 20, 16, 5);
    } else if (kind == 1) {
      stroke(5, 19, 8, 13);
      stroke(8, 13, 18, 3);
      stroke(18, 3, 21, 6);
      stroke(21, 6, 11, 16);
      stroke(11, 16, 5, 19);
      stroke(8, 13, 11, 16);
    } else if (kind == 2) {
      stroke(3, 7, 12, 2);
      stroke(12, 2, 21, 7);
      stroke(21, 7, 12, 12);
      stroke(12, 12, 3, 7);
      stroke(3, 12, 12, 17);
      stroke(12, 17, 21, 12);
      stroke(3, 17, 12, 22);
      stroke(12, 22, 21, 17);
    } else {
      stroke(6, 3, 16, 3);
      stroke(16, 3, 19, 7);
      stroke(19, 7, 19, 21);
      stroke(19, 21, 5, 21);
      stroke(5, 21, 5, 4);
      stroke(5, 4, 6, 3);
      stroke(9, 3, 9, 8);
      stroke(12, 3, 12, 8);
      stroke(15, 3, 15, 8);
    }
  }
  void dashboard() const {
    const auto& window = catalog_->catalogCovers();
    auto& label = label_;
    auto& count = count_;
    const auto& summary = catalog_->catalogSummaryValue();
    if (catalog_->catalogState() == CatalogSurface::State::Error)
      std::snprintf(label, sizeof(label), "%s", tr(STR_CATALOG_LOAD_ERROR));
    else if (catalog_->catalogState() == CatalogSurface::State::Loading)
      std::snprintf(label, sizeof(label), "%s", tr(STR_CATALOG_LOADING_SAVED));
    else if (summary.books.availability != Availability::Ready)
      std::snprintf(label, sizeof(label), "%s", tr(STR_CATALOG_NOT_SAVED));
    else if (!summary.online)
      std::snprintf(label, sizeof(label), "%s", tr(STR_CATALOG_OFFLINE));
    else {
      formatCatalogCount(count, sizeof(count), summary.books.value, true);
      std::snprintf(label, sizeof(label), tr(STR_CATALOG_SAVED_COUNT_FORMAT), count);
    }
    text(catalogRect(16, 68, 448, 18), label, 13);
    const int shelfHeight = textOnly_ ? 172 : 194;
    for (uint8_t n = 0; n < 2; ++n) {
      const int top = 98 + n * (shelfHeight + 12);
      const auto shelf = catalogRect(16, top, 448, shelfHeight);
      textContainer_ = shelf;
      if (textOnly_)
        renderer.drawRect(shelf.x, shelf.y, shelf.width, shelf.height);
      else
        renderer.drawRoundedRect(shelf.x, shelf.y, shelf.width, shelf.height, 1, catalogPixels(12), true);
      text(catalogRect(27, top + 20, 270, 28), n ? tr(STR_CATALOG_RECENT) : tr(STR_CATALOG_DISCOVER), 20, true);
      const auto* span = n < window.windowCount ? &window.windows[n] : nullptr;
      number(catalogRect(310, top + 23, 109, 20), span ? CatalogCount{span->total, span->availability} : CatalogCount{},
             true);
      // Shelf icon belongs to the content header, independently of shared chrome.
      const auto r = catalogRect(428, top + 25, 22, 22);
      if (n) {
        renderer.drawRoundedRect(r.x + catalogPixels(3), r.y + catalogPixels(3), catalogPixels(16), catalogPixels(16),
                                 1, catalogPixels(8), true);
        renderer.drawLine(r.x + r.width / 2, r.y + catalogPixels(6), r.x + r.width / 2, r.y + catalogPixels(12));
        renderer.drawLine(r.x + r.width / 2, r.y + catalogPixels(12), r.x + catalogPixels(17), r.y + catalogPixels(15));
      } else {
        renderer.drawLine(r.x, r.y + catalogPixels(4), r.x + r.width, r.y + catalogPixels(18));
        renderer.drawLine(r.x, r.y + catalogPixels(18), r.x + r.width, r.y + catalogPixels(4));
        renderer.drawLine(r.x + catalogPixels(17), r.y, r.x + r.width, r.y + catalogPixels(4));
        renderer.drawLine(r.x + catalogPixels(17), r.y + catalogPixels(9), r.x + r.width, r.y + catalogPixels(4));
      }
      const uint8_t control = n ? 5 : 0;
      const auto all = catalogRect(307, top + 9, 115, 50);
      hit(all, control);
      if (catalogFocused(control)) renderer.drawRect(all.x, all.y, all.width, all.height, 2, true);
      if (!span || span->availability != Availability::Ready) {
        text(catalogRect(27, top + 76, 420, 28),
             span ? stateTitle(CatalogSurface::catalogAvailabilityState(span->availability, span->count))
                  : stateTitle(catalog_->catalogState()),
             18);
        text(catalogRect(27, top + 119, 200, 24), tr(STR_RETRY), 16, true);
        hit(catalogRect(25, top + 110, 180, 50), control);
        continue;
      }
      for (uint8_t i = 0; i < span->count && i < 4 && span->first + i < window.count; ++i) {
        const auto& book = window.rows[span->first + i];
        const auto item =
            textOnly_ ? catalogRect(27, top + 58 + i * 29, 420, 24) : catalogRect(27 + i * 107, top + 58, 98, 130);
        hit(item, (n ? 6 : 1) + i);
        if (textOnly_)
          text(item, book.title.data(), 17);
        else {
          paintCatalogCover(catalogRect(27 + i * 107, top + 58, 98, 110), book, span->first + i);
          text(catalogRect(27 + i * 107, top + 173, 98, 17), book.title.data(), 12);
        }
      }
    }
    const CatalogCount counts[] = {summary.books, summary.authors, summary.series, summary.onCard};
    const StrId titles[] = {StrId::STR_CATALOG_CATALOG, StrId::STR_HOME_AUTHORS, StrId::STR_HOME_SERIES,
                            StrId::STR_CATALOG_ON_CARD};
    const int top = 98 + 2 * (shelfHeight + 12);
    for (uint8_t n = 0; n < 4; ++n) {
      const int x = 16 + (n % 2) * 230, y = top + (n / 2) * 112;
      const auto tile = catalogRect(x, y, 218, 100);
      textContainer_ = tile;
      if (textOnly_)
        renderer.drawRect(tile.x, tile.y, tile.width, tile.height);
      else
        renderer.drawRoundedRect(tile.x, tile.y, tile.width, tile.height, 1, catalogPixels(12), true);
      if (catalogFocused(n + 10)) renderer.drawRect(tile.x - 3, tile.y - 3, tile.width + 6, tile.height + 6, 2, true);
      text(catalogRect(x + 11, y + 7, 164, 26), I18n::getInstance().get(titles[n]), 20, true);
      icon(tile, n);
      number(catalogRect(x + 11, y + 34, 190, 36), counts[n]);
      const char* subtitle = tr(STR_HOME_BROWSE);
      if (!n) {
        std::snprintf(
            label, sizeof(label),
            summary.collections.value == 1 ? tr(STR_CATALOG_COLLECTION_ONE_FORMAT) : tr(STR_CATALOG_COLLECTIONS_FORMAT),
            unsigned(summary.collections.value));
        subtitle = label;
      } else if (n == 3) {
        std::snprintf(label, sizeof(label), tr(STR_CATALOG_SOURCE_FORMAT), unsigned(summary.linked.value),
                      summary.sourceLabel.data());
        subtitle = label;
      }
      text(catalogRect(x + 11, y + (textOnly_ ? 73 : 75), 196, 19), subtitle, 14);
      hit(tile, n + 10);
    }
    textContainer_ = safe();
  }
  void catalogRowMetadata(char (&value)[96], const QueryEntry& entry, const char* fileText,
                          bool transferText = false) const {
    const char* reading = entry.status == QueryShow::Finished    ? tr(STR_CATALOG_FINISHED)
                          : entry.status == QueryShow::Unread    ? tr(STR_CATALOG_UNREAD)
                          : entry.status == QueryShow::Reading   ? tr(STR_HOME_PAGE_READING)
                          : entry.status == QueryShow::Rereading ? tr(STR_CATALOG_REREADING)
                          : entry.status == QueryShow::OnHold    ? tr(STR_CATALOG_ON_HOLD)
                          : entry.status == QueryShow::Abandoned ? tr(STR_CATALOG_ABANDONED)
                                                                 : nullptr;
    char progress[48];
    if (!transferText && reading) {
      if ((entry.status == QueryShow::Reading || entry.status == QueryShow::Rereading) && entry.book.readingKnown &&
          entry.book.pages) {
        const auto percent = uint64_t(entry.book.page) * 100 / entry.book.pages;
        std::snprintf(progress, sizeof(progress), tr(STR_CATALOG_STATUS_PERCENT_FORMAT), reading,
                      unsigned(percent > 100 ? 100 : percent));
        reading = progress;
      }
      std::snprintf(value, sizeof(value), "%s · %s", fileText, reading);
    } else {
      std::snprintf(value, sizeof(value), "%s", fileText);
    }
  }
  void list() const {
    using Panel = CatalogSurface::Panel;
    using State = CatalogSurface::State;
    const auto panel = catalog_->catalogPanel();
    const auto state = catalog_->catalogState();
    const auto count = catalog_->catalogVisibleRows();
    auto& label = label_;
    auto& numberValue = count_;
    auto& detail = detail_;
    if (panel == Panel::Browse) {
      text(catalogRect(16, 68, 448, 20), tr(STR_CATALOG_INDEXES), 14);
      for (uint8_t i = 0; i < catalog_->catalogBrowseCount(); ++i) {
        const int x = 16 + (i % 2) * 230, y = 98 + (i / 2) * 103;
        const auto tile = catalogRect(x, y, 218, 91);
        textContainer_ = tile;
        renderer.drawRoundedRect(tile.x, tile.y, tile.width, tile.height, 1, catalogPixels(8), true);
        if (catalogFocused(i)) renderer.drawRect(tile.x - 3, tile.y - 3, tile.width + 6, tile.height + 6, 2, true);
        const auto root = catalog_->catalogBrowseRootAt(i);
        text(catalogRect(x + 13, y + 13, 192, 25), rootTitle(root), 17);
        const auto value = catalog_->catalogRootCount(root);
        if (value.availability == Availability::Ready)
          formatCatalogCount(numberValue, sizeof(numberValue), value.value, true);
        else
          std::snprintf(numberValue, sizeof(numberValue), "—");
        std::snprintf(label, sizeof(label), "%s ›", numberValue);
        text(catalogRect(x + 13, y + 50, 192, 23), label, 15);
        hit(tile, i);
      }
      textContainer_ = safe();
      return;
    }
    const bool groups = panel == Panel::Groups, sources = panel == Panel::Sources;
    if (!sources) {
      const auto filter = catalogRect(16, 68, groups ? 75 : 61, 48);
      renderer.drawRect(filter.x, filter.y, filter.width, filter.height, catalogFocused(count) ? 3 : 1, true);
      text(catalogRect(28, 83, groups ? 62 : 45, 22), groups ? tr(STR_CATALOG_FILTER_ACTIVE) : tr(STR_CATALOG_FILTER),
           15);
      hit(filter, count, 5);
      if (groups) {
        const auto chip = catalogRect(101, 68, 122, 48);
        renderer.drawRect(chip.x, chip.y, chip.width, chip.height, catalogFocused(count * 2 + 3) ? 3 : 1, true);
        text(catalogRect(113, 83, 104, 22),
             catalog_->catalogRoot() == CatalogRoot::Authors ? tr(STR_CATALOG_BY_AUTHOR_REMOVE)
                                                             : tr(STR_CATALOG_BY_SERIES_REMOVE),
             15);
        hit(chip, count * 2 + 3);
      }
    }
    const bool inlineSelection = catalog_->catalogPreparing() || catalog_->catalogInlineError() != DownloadError::None;
    const bool completed =
        catalog_->catalogTransferValue().state == DownloadState::Complete && catalog_->catalogSelectedBook().title[0];
    if (state == State::NotSaved)
      std::snprintf(label, sizeof(label), "%s", tr(STR_HOME_NOT_SAVED));
    else if (state == State::Error)
      std::snprintf(label, sizeof(label), "%s", tr(STR_CATALOG_LIST_ERROR));
    else if (state == State::Unsupported)
      std::snprintf(label, sizeof(label), "%s", tr(STR_CATALOG_UNAVAILABLE_SOURCE));
    else if (inlineSelection)
      std::snprintf(label, sizeof(label), "%s", catalog_->catalogSelectedBook().title.data());
    else if (completed)
      std::snprintf(label, sizeof(label), "%s", tr(STR_CATALOG_COMPLETE_RETAINED));
    else if (!sources && !catalog_->catalogResult().membershipComplete)
      std::snprintf(label, sizeof(label), "%s", tr(STR_CATALOG_INCOMPLETE));
    else {
      formatCatalogCount(numberValue, sizeof(numberValue), catalog_->catalogTotal(), true);
      std::snprintf(label, sizeof(label),
                    sources                                          ? tr(STR_CATALOG_LISTS_COUNT_FORMAT)
                    : catalog_->catalogRoot() == CatalogRoot::OnCard ? tr(STR_CATALOG_VERIFIED_FORMAT)
                    : groups ? (catalog_->catalogRoot() == CatalogRoot::Authors ? tr(STR_CATALOG_AUTHOR_COUNT_FORMAT)
                                                                                : tr(STR_CATALOG_SERIES_COUNT_FORMAT))
                    : state == State::Empty                                     ? tr(STR_CATALOG_EMPTY_COUNT_FORMAT)
                    : catalog_->catalogScoped()                                 ? tr(STR_CATALOG_SCOPE_COUNT_FORMAT)
                    : catalog_->catalogDialog() != CatalogSurface::Dialog::None ? tr(STR_CATALOG_ALL_COUNT_FORMAT)
                                                                                : tr(STR_CATALOG_ORDER_COUNT_FORMAT),
                    numberValue);
    }
    text(catalogRect(16, sources ? 68 : 130, 448, 22), label, 14);
    if ((state == State::NotSaved || state == State::Error || state == State::Unsupported || state == State::Empty) &&
        !count) {
      const char* message = state == State::NotSaved      ? tr(STR_CATALOG_LIST_NOT_SAVED)
                            : state == State::Error       ? tr(STR_CATALOG_LIST_ERROR_HELP)
                            : state == State::Unsupported ? tr(STR_CATALOG_LISTS_UNSUPPORTED_HELP)
                                                          : tr(STR_CATALOG_EMPTY);
      text(catalogRect(16, sources ? 110 : 190, 448, 60), message, 20);
      if (state != State::Unsupported)
        button(catalogRect(16, 270, 120, 50), state == State::Empty ? tr(STR_CATALOG_FILTER) : tr(STR_RETRY),
               state == State::Empty ? count : count + 3);
    }
    for (uint8_t i = 0; i < count && (catalog_->catalogDialog() == CatalogSurface::Dialog::None || i < 3); ++i) {
      if (sources) {
        const int y = 106 + i * 72;
        const auto row = catalogRect(16, y, 448, 60);
        textContainer_ = row;
        renderer.drawRect(row.x, row.y, row.width, row.height, catalogFocused(i) ? 3 : 1, true);
        const auto& source = catalog_->catalogSourcesValue().rows[i];
        text(catalogRect(29, y + 20, 255, 24), source.title.data(), 18);
        if (source.books.availability == Availability::Ready)
          std::snprintf(label, sizeof(label), tr(STR_CATALOG_SOURCE_BOOKS_FORMAT), unsigned(source.books.value));
        else
          std::snprintf(label, sizeof(label), "— ›");
        text(catalogRect(290, y + 22, 161, 22), label, 15, false, 2);
        hit(row, i);
        continue;
      }
      const auto& entry = catalog_->catalogResult().entries[i];
      const int y = 160 + i * (groups ? 159 : textOnly_ ? 83 : 91);
      if (groups) {
        const auto group = catalogRect(16, y, 448, 146);
        textContainer_ = group;
        text(catalogRect(16, y + 9, 290, 28),
             entry.missing ? (catalog_->catalogRoot() == CatalogRoot::Series ? tr(STR_CATALOG_NO_SERIES)
                                                                             : tr(STR_CATALOG_UNKNOWN_AUTHOR))
                           : entry.label.data(),
             19, true);
        if (!catalog_->catalogResult().membershipComplete)
          std::snprintf(label, sizeof(label), "%s", tr(STR_CATALOG_COUNT_UNKNOWN));
        else if (entry.books == 1)
          std::snprintf(label, sizeof(label), "%s", tr(STR_CATALOG_ONE_BOOK));
        else
          std::snprintf(label, sizeof(label), tr(STR_CATALOG_ALL_FORMAT), unsigned(entry.books));
        text(catalogRect(311, y + 13, 153, 22), label, entry.books > 1 ? 14 : 13, entry.books > 1, 2);
        text(catalogRect(16, y + 43, 448, 20),
             catalog_->catalogRoot() == CatalogRoot::Authors ? tr(STR_CATALOG_TITLE_ORDER)
                                                             : tr(STR_CATALOG_SERIES_ORDER),
             13);
        if (entry.books > 1 && catalog_->catalogResult().membershipComplete) {
          const auto all = catalogRect(308, y, 156, 42);
          hit(all, i, 4);
          if (catalogFocused(count + 3 + i)) renderer.drawRect(all.x, all.y, all.width, all.height, 2, true);
        }
        if (catalog_->catalogRoot() == CatalogRoot::Series && !entry.missing) hit(catalogRect(16, y, 290, 42), i, 2);
      }
      const int rowY = y + (groups ? 64 : 0), rowH = groups ? 80 : textOnly_ ? 75 : 83;
      const auto row = catalogRect(16, rowY, 448, rowH);
      textContainer_ = row;
      if (catalogFocused(i))
        renderer.drawRect(row.x - catalogPixels(3), row.y - catalogPixels(3), row.width + catalogPixels(6),
                          row.height + catalogPixels(6), textOnly_ ? 2 : 3, true);
      hit(row, i);
      const int inset = textOnly_ ? 4 + (catalogFocused(i) ? 22 : 0) : 59;
      if (!textOnly_) {
        const auto cover = catalogRect(20, rowY + (groups ? 8 : 9), 43, 65);
        paintCatalogCover(cover, entry.book, i, groups ? 1 : unsigned(i + 1));
      } else if (catalogFocused(i))
        text(catalogRect(20, rowY + 25, 16, 25), ">", 19, true);
      const int titleInset = groups ? 15 : textOnly_ ? 13 : 17;
      text(catalogRect(16 + inset, rowY + titleInset, 448 - inset - 44, 28), entry.book.title.data(), 19, true);
      const bool selected =
          entry.book.key == catalog_->catalogSelectedBook().key && catalog_->catalogSelectedBook().title[0];
      const char* file = entry.onDevice == QueryTruth::Yes  ? tr(STR_CATALOG_ON_CARD)
                         : entry.onDevice == QueryTruth::No ? tr(STR_CATALOG_SERVER_ONLY)
                                                            : tr(STR_HOME_NOT_SAVED);
      const bool transfer =
          selected && (catalog_->catalogPreparing() || catalog_->catalogInlineError() != DownloadError::None ||
                       catalog_->catalogAvailabilityUnknown());
      if (transfer)
        file = catalog_->catalogPreparing()             ? tr(STR_CATALOG_PREPARING)
               : catalog_->catalogAvailabilityUnknown() ? tr(STR_HOME_NOT_SAVED)
               : catalog_->catalogChoices().availability == Availability::Unavailable
                   ? tr(STR_CATALOG_ACCESS)
                   : errorTitle(catalog_->catalogInlineError());
      catalogRowMetadata(detail, entry, file, transfer);
      if (selected && completed)
        std::snprintf(label, sizeof(label), "%s · %s", entry.book.title.data(), tr(STR_CATALOG_ON_CARD));
      else if (!groups && entry.label[0] && !transfer)
        std::snprintf(label, sizeof(label), "%s · %s", entry.label.data(), detail);
      else
        std::snprintf(label, sizeof(label), "%s", detail);
      text(catalogRect(16 + inset, rowY + titleInset + 32, 448 - inset - 44, 36), label, 14);
      text(catalogRect(425, rowY + 18, 32, 36), "⋯", 26, false, 1);
      hit(catalogRect(415, rowY, 49, rowH), i, 1);
      // Row separators are deliberately left for FK.
    }
    textContainer_ = safe();
    if (catalog_->catalogInlineError() == DownloadError::NoSpace)
      button(catalogRect(16, 650, 110, 50), tr(STR_RETRY), count + 3);
  }
  void formatCatalogBytes(char* out, size_t capacity, uint64_t value) const {
    if (value >= 1000000)
      std::snprintf(out, capacity, tr(STR_CATALOG_MB_FORMAT), (unsigned long long)(value / 1000000),
                    (unsigned long long)(value % 1000000 / 100000));
    else if (value >= 1000)
      std::snprintf(out, capacity, tr(STR_CATALOG_KB_FORMAT), (unsigned long long)(value / 1000));
    else
      std::snprintf(out, capacity, tr(STR_CATALOG_FILE_BYTES_FORMAT), (unsigned long long)value);
  }
  void paintCatalogDialog() const {
    using Dialog = CatalogSurface::Dialog;
    const auto dialog = catalog_->catalogDialog();
    const auto& job = catalog_->catalogTransferValue();
    const bool choices = dialog == Dialog::Choices, offline = dialog == Dialog::Offline;
    const int height = choices ? 229 + catalog_->catalogChoices().count * 64 : offline ? 231 : 225;
    const int top = (800 - height) / 2;
    // Ordered white stipple represents the approved dimmed background in 1 bit.
    const auto s = safe();
    for (int y = s.y; y < s.y + s.height; ++y)
      for (int x = s.x; x < s.x + s.width; ++x)
        if ((x + y * 2) % 5 >= 2) renderer.drawPixel(x, y, false);
    const auto modal = catalogRect(24, top, 432, height);
    for (int y = modal.y + catalogPixels(5); y < modal.y + modal.height + catalogPixels(5); ++y)
      for (int x = modal.x + catalogPixels(5); x < modal.x + modal.width + catalogPixels(5); ++x)
        renderer.drawPixel(x, y, (x + y * 2) % 5 < 2);
    renderer.fillRect(modal.x, modal.y, modal.width, modal.height, false);
    renderer.drawRect(modal.x, modal.y, modal.width, modal.height, 2, true);
    textContainer_ = modal;
#ifdef SIMULATOR
    geometryLayer = 1;
#endif
    const char* title = choices                                ? tr(STR_DOWNLOAD)
                        : offline                              ? tr(STR_CATALOG_OFFLINE_PROMPT)
                        : dialog == Dialog::Login              ? tr(STR_CATALOG_DOWNLOAD_UNAVAILABLE)
                        : job.state == DownloadState::Canceled ? tr(STR_CATALOG_CANCELED)
                        : job.state == DownloadState::Failed   ? tr(STR_CATALOG_STOPPED)
                                                               : tr(STR_DOWNLOADING);
    text(catalogRect(46, top + 22, 388, offline ? 60 : 32), title, 23, true);
    text(catalogRect(46, top + (offline ? 99 : 70), 388, 26), catalog_->catalogSelectedBook().title.data(), 18);
    auto& label = label_;
    auto& received = count_;
    auto& total = total_;
    if (choices) {
      std::snprintf(label, sizeof(label), tr(STR_CATALOG_DESTINATION_FORMAT),
                    catalog_->catalogChoices().destination[0] ? catalog_->catalogChoices().destination.data()
                                                              : tr(STR_CATALOG_DESTINATION));
    } else if (dialog == Dialog::Login)
      std::snprintf(label, sizeof(label), "%s", tr(STR_CATALOG_NO_LOGIN));
    else if (job.state == DownloadState::Canceled)
      std::snprintf(label, sizeof(label), "%s", tr(STR_CATALOG_CANCELED_HELP));
    else if (job.state == DownloadState::Failed)
      std::snprintf(label, sizeof(label), "%s",
                    job.error == DownloadError::Unavailable ? tr(STR_CATALOG_CONNECTION_LOST) : errorTitle(job.error));
    else {
      formatCatalogBytes(received, sizeof(received), job.received);
      formatCatalogBytes(total, sizeof(total), job.total);
      if (job.totalKnown && job.total)
        std::snprintf(label, sizeof(label), tr(STR_CATALOG_HUMAN_PROGRESS_FORMAT), received, total,
                      unsigned(job.received >= job.total ? 100 : double(job.received) * 100 / job.total));
      else
        std::snprintf(label, sizeof(label), tr(STR_CATALOG_HUMAN_RECEIVED_FORMAT), received);
    }
    if (!offline) text(catalogRect(46, top + 109, 388, 48), label, 18);
    hitCount_ = 0;
    if (choices) {
      for (uint8_t i = 0; i < catalog_->catalogChoices().count; ++i) {
        const int y = top + 148 + i * 64;
        const auto action = catalogRect(46, y, 388, 52);
        renderer.drawRect(action.x, action.y, action.width, action.height, catalogFocused(i) ? 3 : 1, true);
        text(catalogRect(59, y + 16, 255, 24), catalog_->catalogChoices().rows[i].label.data(), 16, true);
        const auto& choice = catalog_->catalogChoices().rows[i];
        if (choice.bytesKnown)
          formatCatalogBytes(label, sizeof(label), choice.bytes);
        else
          std::snprintf(label, sizeof(label), "%s", tr(STR_CATALOG_SIZE_UNKNOWN));
        text(catalogRect(320, y + 18, 101, 20), label, 14, false, 2);
        hit(action, i);
      }
      button(catalogRect(46, top + 154 + catalog_->catalogChoices().count * 64, 388, 52), tr(STR_CANCEL),
             catalog_->catalogChoices().count);
    } else {
      const int y = top + (offline ? 141 : 150), h = offline ? 68 : 52;
      const bool resume = job.state == DownloadState::Failed || job.state == DownloadState::Canceled ||
                          job.state == DownloadState::Paused;
      button(catalogRect(46, y, 188, h),
             offline                   ? tr(STR_CATALOG_CONNECT)
             : dialog == Dialog::Login ? tr(STR_CATALOG_KEEP_BROWSING)
             : resume                  ? tr(STR_CATALOG_RESUME)
                                       : tr(STR_CANCEL),
             0);
      button(catalogRect(246, y, 188, h),
             offline                   ? tr(STR_CANCEL)
             : dialog == Dialog::Login ? tr(STR_CATALOG_SETUP)
                                       : tr(STR_BACK),
             1);
    }
    textContainer_ = safe();
  }
  void paintCatalogFooter() const {
    char label[48];
    const auto panel = catalog_->catalogPanel();
    std::snprintf(label, sizeof(label), "%s", tr(STR_CATALOG_SAVED_VIEW));
    if (panel == CatalogSurface::Panel::Dashboard)
      std::snprintf(label, sizeof(label), "‹ %s ›", tr(STR_CATALOG_TITLE));
    else if (catalog_->catalogDialog() == CatalogSurface::Dialog::None &&
             (catalog_->catalogHasMore() || catalog_->catalogOffset()) &&
             catalog_->catalogResult().membershipComplete) {
      const auto total =
          panel == CatalogSurface::Panel::Groups ? catalog_->catalogResult().optionTotal : catalog_->catalogTotal();
      const auto pages = (total + catalog_->catalogPageRows() - 1) / catalog_->catalogPageRows();
      std::snprintf(label, sizeof(label), "‹ %u / %u ›",
                    unsigned(catalog_->catalogOffset() / catalog_->catalogPageRows() + 1), unsigned(pages ? pages : 1));
    }
    const auto r = catalogRect(0, 752, 480, 48);
    renderer.drawLine(r.x, r.y, r.x + r.width, r.y);
    text(catalogRect(16, 769, 180, 22), mappedInput.hasTouch() ? tr(STR_BACK) : tr(STR_CATALOG_BACK_SELECT), 14);
    text(catalogRect(240, 769, 224, 22), label, 14, false, 2);
    if (panel != CatalogSurface::Panel::Dashboard && panel != CatalogSurface::Panel::Browse) {
      const auto count = catalog_->catalogVisibleRows();
      if (catalog_->catalogOffset()) hit(catalogRect(324, 752, 65, 48), count + 1);
      if (catalog_->catalogHasMore()) hit(catalogRect(399, 752, 65, 48), count + 2);
    }
  }

 public:
  CatalogActivity(GfxRenderer& renderer, MappedInputManager& input, Library& library)
      : Activity("Library", renderer, input), library_(library) {}
  void onEnter() override {
    Activity::onEnter();
    ownsLocal_ = !localLibraryActive();
    enterLocalLibrary();
#if CROSSINK_APP_CAP_HOME_EDITOR_SETTINGS
    textOnly_ = true;
#else
    textOnly_ = false;
#endif
    // <=32 KiB reused projection/filter/cursor workspace exceeds the small task
    // stack and is per-owner, so static shared storage would leak between screens.
    catalog_ = makeUniqueNoThrow<CatalogSurface>(library_);
    if (!catalog_) {
      LOG_ERR("BSC", "Cannot allocate catalog surface (%u bytes)", unsigned(sizeof(CatalogSurface)));
      finish();
      return;
    }
    if (!textOnly_) {
      bitmaps_ = makeUniqueNoThrow<CoverBitmap[]>(8);
      if (!bitmaps_)
        LOG_ERR("BSC", "Cannot allocate thumbnails (%u bytes); using placeholders", unsigned(8 * sizeof(CoverBitmap)));
    }
    LOG_INF("BSC", "Catalog bounded allocation: controller=%u thumbnails=%u", unsigned(sizeof(CatalogSurface)),
            unsigned(bitmaps_ ? 8 * sizeof(CoverBitmap) : 0));
    catalog_->enterCatalog(textOnly_);
#ifdef SIMULATOR
    sceneController = catalog_.get();
#endif
    requestUpdate();
  }
  void onExit() override {
#ifdef SIMULATOR
    if (sceneController == catalog_.get()) sceneController = nullptr;
#endif
    if (catalog_) catalog_->retireCatalog();
    bitmaps_.reset();
    catalog_.reset();
    if (ownsLocal_) leaveLocalLibrary();
    Activity::onExit();
  }
  void loop() override {
    if (!catalog_ || !networkDisplayReady()) return;
    if (!networkAdmission().busy()) {
      RenderLock lock(RenderLock::Mode::Try);
      if (lock.ownsLock()) workLocalLibrary();
    }
    {
      RenderLock lock;
      bool update = catalog_->pollCatalog();
      if (!catalog_->catalogLive()) {
        finish();
        return;
      }
      for (uint8_t i = 0; bitmaps_ && i < catalog_->catalogCoverCount(); ++i) {
        const auto ref = catalog_->catalogCover(i);
        if (!(ref == loaded_[i])) {
          loaded_[i] = ref;
          bitmaps_[i].width = bitmaps_[i].height = 0;
          // References are immutable. A rejected reference remains a placeholder
          // until a new publication, rather than redrawing/retrying every loop.
          if (ref.id) library_.readCover(ref, bitmaps_[i]);
          update = true;
        }
      }
      if (!mappedInput.isPhysicalPressed(MappedInputManager::Button::Confirm)) longConfirmHandled_ = false;
      if (!longConfirmHandled_ && catalog_->catalogDialog() == CatalogSurface::Dialog::None &&
          mappedInput.isPressed(MappedInputManager::Button::Confirm) && mappedInput.getHeldTime() >= 1000) {
        longConfirmHandled_ = true;
        mappedInput.suppressNextConfirmRelease();
        if (catalog_->catalogPanel() == CatalogSurface::Panel::Groups && catalog_->catalogRoot() == CatalogRoot::Series)
          catalog_->catalogSeries(catalog_->catalogFocus());
        else
          catalog_->catalogDetails(catalog_->catalogFocus());
        requestUpdate();
        return;
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        catalog_->backCatalog();
        update = true;
      } else if (mappedInput.wasReleased(mappedInput.menuButton(MappedInputManager::Button::Down))) {
        catalog_->stepCatalog(1);
        update = true;
      } else if (mappedInput.wasReleased(mappedInput.menuButton(MappedInputManager::Button::Up))) {
        catalog_->stepCatalog(-1);
        update = true;
      } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        catalog_->activateCatalog();
        update = true;
      } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
        catalog_->catalogDetails(catalog_->catalogFocus());
        update = true;
      } else if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
        catalog_->catalogSeries(catalog_->catalogFocus());
        update = true;
      }
      int x = 0, y = 0;
      unsigned long heldMs = 0;
      if (mappedInput.wasScreenTapped(x, y, heldMs)) {
        const auto bounds = safe();
        if (x >= bounds.x && x < bounds.x + 80 && y >= bounds.y && y < headerBottom())
          catalog_->backCatalog();
        else
          for (uint8_t n = hitCount_; n > 0; --n) {
            const auto& h = hits_[n - 1];
            if (x >= h.box.x && x < h.box.x + h.box.width && y >= h.box.y && y < h.box.y + h.box.height) {
              if (h.action == 5)
                catalog_->catalogFilter();
              else if (h.action == 1)
                catalog_->catalogDetails(h.index);
              else if (h.action == 2)
                catalog_->catalogSeries(h.index);
              else if (heldMs >= 1000 && (((catalog_->catalogPanel() == CatalogSurface::Panel::Books ||
                                            catalog_->catalogPanel() == CatalogSurface::Panel::Groups) &&
                                           h.index < catalog_->catalogVisibleRows()) ||
                                          (catalog_->catalogPanel() == CatalogSurface::Panel::Dashboard &&
                                           h.index > 0 && h.index < 10 && h.index != 5)))
                catalog_->catalogDetails(h.index);
              else {
                catalog_->selectCatalogControl(h.action == 4 ? catalog_->catalogVisibleRows() + 3 + h.index : h.index);
                catalog_->activateCatalog();
              }
              break;
            }
          }
        update = true;
      }
      if (!catalog_->catalogLive()) {
        finish();
        return;
      }
      if (update) requestUpdate();
    }
    if (takeCatalogAccountRequest()) {
      showServiceChoice(renderer, mappedInput, library_);
      return;
    }
    std::string path;
    if (takeIndexedOpenRequest(path)) activityManager.goToReader(path);
  }
  void render(RenderLock&&) override {
    if (!catalog_ || !catalog_->catalogLive()) return;
#ifdef SIMULATOR
    ++paints;
#endif
    hitCount_ = 0;
    renderer.clearScreen();
    const auto s = safe();
    textContainer_ = s;
#ifdef SIMULATOR
    catalogGeometryCount = 0;
    geometryLayer = 0;
#endif
    text(catalogRect(16, 15, 54, 26), tr(STR_BACK), 15);
    text(catalogRect(76, 14, 388, 32),
         catalog_->catalogPanel() == CatalogSurface::Panel::Dashboard ? tr(STR_CATALOG_TITLE)
         : catalog_->catalogPanel() == CatalogSurface::Panel::Browse  ? tr(STR_CATALOG_BROWSE_TITLE)
         : catalog_->catalogScopeTitle()[0]                           ? catalog_->catalogScopeTitle()
                                                                      : rootTitle(catalog_->catalogRoot()),
         23, true);
    renderer.drawLine(s.x, headerBottom(), s.x + s.width, headerBottom());
    if (catalog_->catalogPanel() == CatalogSurface::Panel::Dashboard)
      dashboard();
    else
      list();
    paintCatalogFooter();
    if (catalog_->catalogDialog() != CatalogSurface::Dialog::None) paintCatalogDialog();
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
};
}  // namespace
std::unique_ptr<Activity> createCatalogScreen(GfxRenderer& renderer, MappedInputManager& input, Library& library) {
  auto screen = makeUniqueNoThrow<CatalogActivity>(renderer, input, library);
  if (!screen) LOG_ERR("BSC", "Cannot allocate catalog activity");
  return screen;
}
#ifdef SIMULATOR
void showCatalogScene(GfxRenderer& renderer, MappedInputManager& input, Library& library) {
  auto screen = createCatalogScreen(renderer, input, library);
  if (screen) activityManager.replaceActivity(std::move(screen));
}
uint32_t catalogScenePaints() { return paints; }
bool writeCatalogGeometry(const char* path) {
  FILE* file = std::fopen(path, "w");
  if (!file) return false;
  std::fprintf(file, "{\"capacity_exhausted\":%s,\"texts\":[",
               catalogGeometryCount >= catalogGeometry.size() ? "true" : "false");
  for (unsigned n = 0; n < catalogGeometryCount; ++n) {
    const auto& value = catalogGeometry[n];
    std::fprintf(file, "%s{\"label\":\"", n ? "," : "");
    for (unsigned char c : value.label) {
      if (!c) break;
      if (c == '"' || c == '\\') std::fputc('\\', file);
      if (c >= 32) std::fputc(c, file);
    }
    std::fprintf(file, "\",\"layer\":%u", unsigned(value.layer));
    const Rect values[] = {value.ink, value.slot, value.container};
    const char* keys[] = {"ink", "slot", "container"};
    for (unsigned i = 0; i < 3; ++i) {
      const auto r = values[i];
      std::fprintf(file, ",\"%s\":[%d,%d,%d,%d]", keys[i], r.x, r.y, r.width, r.height);
    }
    std::fputc('}', file);
  }
  std::fprintf(file, "]}\n");
  return std::fclose(file) == 0;
}

CatalogSurface* catalogSceneController() { return sceneController; }
#endif
}  // namespace booksync
