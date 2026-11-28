// SPDX-License-Identifier: GPL-3.0-or-later
#include "CatalogScreen.h"

#include <AppCapabilities.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

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
  CatalogRoute entry_{};
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
  bool ownsLocal_ = false, textOnly_ = true, longConfirmHandled_ = false;
  Rect safe() const { return UITheme::getInstance().getScreenSafeArea(renderer, true, false); }
  int headerBottom() const {
    const auto r = safe();
    return UITheme::getHeaderRect(renderer, UITheme::getInstance().getMetrics().headerHeight, r).y +
           UITheme::getInstance().getMetrics().headerHeight;
  }
  void text(Rect r, const char* label, int font = UI_10_FONT_ID) const {
    if (r.width <= 0 || r.height <= 0) return;
    renderer.beginTextClip(r.x, r.y, r.width, r.height);
    renderer.drawText(font, r.x, r.y, label);
    renderer.endTextClip();
  }
  void hit(Rect r, uint8_t index, uint8_t action = 0) const {
    if (hitCount_ < hits_.size()) hits_[hitCount_++] = {r, index, action};
  }
  void button(Rect r, const char* label, uint8_t index, uint8_t action = 0) const {
    const bool focus = action == 0 && index == catalog_->catalogFocus();
    renderer.drawRect(r.x, r.y, r.width, r.height, focus ? 3 : 1, true);
    text(Rect{r.x + 9, r.y + 8, r.width - 18, r.height - 12}, label);
    hit(r, index, action);
  }
  void number(Rect r, CatalogCount count, bool all = false) const {
    char value[48];
    if (count.availability == Availability::Ready)
      std::snprintf(value, sizeof(value), all ? tr(STR_CATALOG_ALL_FORMAT) : "%u", unsigned(count.value));
    else
      std::snprintf(value, sizeof(value), "%s", all ? tr(STR_CATALOG_ALL_UNKNOWN) : "—");
    text(r, value, all ? UI_10_FONT_ID : BITTER_16_FONT_ID);
  }
  void paintCatalogCover(Rect r, const BookRow& book, uint8_t index) const {
    if (r.width <= 2 || r.height <= 2) return;
    if (textOnly_) {
      text(r, book.title.data());
      return;
    }
    renderer.drawRect(r.x, r.y, r.width, r.height);
    const CoverBitmap* bitmap = bitmaps_ && index < loaded_.size() && loaded_[index].id ? &bitmaps_[index] : nullptr;
    if (!bitmap || !bitmap->width || !bitmap->height || bitmap->width > CoverWidth || bitmap->height > CoverHeight) {
      text(Rect{r.x + 5, r.y + 6, r.width - 10, r.height - 12}, book.title.data());
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
    // Original small outline glyphs; whole tiles remain the interaction targets.
    const int x = r.x + r.width - 24, y = r.y + 10;
    if (kind == 1) {
      renderer.drawLine(x, y + 16, x + 15, y);
      renderer.drawLine(x + 4, y + 19, x + 18, y + 3);
      renderer.drawLine(x, y + 16, x + 4, y + 19);
    } else if (kind == 2) {
      for (int n = 0; n < 3; ++n) renderer.drawRect(x + n * 2, y + n * 5, 15, 8);
    } else {
      renderer.drawRect(x, y, 16, 21);
      if (kind == 0)
        renderer.drawLine(x + 5, y, x + 5, y + 21);
      else
        renderer.drawLine(x + 3, y + 5, x + 12, y + 5);
    }
  }
  void dashboard() const {
    const auto s = safe();
    const int line = renderer.getLineHeight(UI_10_FONT_ID);
    const auto& window = catalog_->catalogCovers();
    bool failed = catalog_->catalogState() == CatalogSurface::State::Error;
    for (uint8_t i = 0; i < window.windowCount && i < 2; ++i)
      failed = failed || window.windows[i].availability == Availability::Unavailable;
    int top = headerBottom() + 10;
    text(Rect{s.x + 12, top, s.width - 24, line + 4},
         failed                                                                         ? tr(STR_CATALOG_LOAD_ERROR)
         : catalog_->catalogState() == CatalogSurface::State::Loading                   ? tr(STR_HOME_LOADING)
         : catalog_->catalogSummaryValue().books.availability == Availability::NotSaved ? tr(STR_HOME_NOT_SAVED)
         : catalog_->catalogSummaryValue().online                                       ? tr(STR_CATALOG_SAVED)
                                                                                        : tr(STR_CATALOG_OFFLINE));
    top += line + 12;
    const int bottom = s.y + s.height - 60;
    const int gap = 10;
    const int tileH = (bottom - top) * 17 / 100;
    const int shelfH = (bottom - top - tileH * 2 - gap * 3) / 2;
    for (uint8_t n = 0; n < 2; ++n) {
      Rect box{s.x + 12, top + n * (shelfH + gap), s.width - 24, shelfH};
      renderer.drawRect(box.x, box.y, box.width, box.height);
      text(Rect{box.x + 9, box.y + 8, box.width - 160, line + 4}, n ? tr(STR_CATALOG_RECENT) : tr(STR_CATALOG_DISCOVER),
           UI_12_FONT_ID);
      const WindowSpan* span = n < window.windowCount ? &window.windows[n] : nullptr;
      const Rect all{box.x + box.width - 146, box.y + 2, 140, 50};
      number(Rect{all.x, all.y + 8, all.width, line + 4},
             span ? CatalogCount{span->total, span->availability} : CatalogCount{}, true);
      if (catalog_->catalogFocus() == (n ? 5 : 0)) renderer.drawRect(all.x, all.y, all.width, all.height, 3, true);
      hit(all, n ? 5 : 0);
      if (!span || span->availability != Availability::Ready) {
        text(Rect{box.x + 10, box.y + 54, box.width - 20, line + 4},
             span ? stateTitle(CatalogSurface::catalogAvailabilityState(span->availability, span->count))
                  : stateTitle(catalog_->catalogState()));
        if ((span && span->availability == Availability::Unavailable) ||
            (!span && catalog_->catalogState() == CatalogSurface::State::Error))
          button(Rect{box.x + 10, box.y + 60 + line, 110, 46}, tr(STR_RETRY), n ? 5 : 0);
        continue;
      }
      const auto rows = span->count > 4 ? 4 : span->count;
      if (!rows) text(Rect{box.x + 10, box.y + 54, box.width - 20, line + 4}, tr(STR_CATALOG_EMPTY));
      for (uint8_t i = 0; i < rows && span->first + i < window.count; ++i) {
        const auto& book = window.rows[span->first + i];
        const uint8_t control = (n ? 6 : 1) + i;
        hit(Rect{box.x + 8 + (textOnly_ ? 0 : i * (box.width - 24) / 4), box.y + 50 + (textOnly_ ? i * line : 0),
                 textOnly_ ? box.width - 16 : (box.width - 24) / 4, textOnly_ ? line : box.height - 56},
            control);
        if (textOnly_) {
          text(Rect{box.x + 10, box.y + 52 + i * line, box.width - 20, line}, book.title.data());
        } else {
          const int w = (box.width - 24) / 4;
          paintCatalogCover(Rect{box.x + 8 + i * w, box.y + 50, w - 7, box.height - line - 62}, book, span->first + i);
          text(Rect{box.x + 8 + i * w, box.y + box.height - line - 5, w - 7, line}, book.title.data(), SMALL_FONT_ID);
        }
      }
    }
    const CatalogCount counts[] = {catalog_->catalogSummaryValue().books, catalog_->catalogSummaryValue().authors,
                                   catalog_->catalogSummaryValue().series, catalog_->catalogSummaryValue().onCard};
    const StrId labels[] = {StrId::STR_CATALOG_CATALOG, StrId::STR_HOME_AUTHORS, StrId::STR_HOME_SERIES,
                            StrId::STR_CATALOG_ON_CARD};
    const int tileTop = top + 2 * (shelfH + gap);
    for (uint8_t n = 0; n < 4; ++n) {
      Rect box{s.x + 12 + (n % 2) * ((s.width - 24 - gap) / 2 + gap), tileTop + (n / 2) * (tileH + gap),
               (s.width - 24 - gap) / 2, tileH};
      renderer.drawRect(box.x, box.y, box.width, box.height, catalog_->catalogFocus() == n + 10 ? 3 : 1, true);
      text(Rect{box.x + 8, box.y + 7, box.width - 40, line}, I18n::getInstance().get(labels[n]), UI_12_FONT_ID);
      icon(box, n);
      number(Rect{box.x + 8, box.y + line + 8, box.width - 16, tileH - line - 12}, counts[n]);
      char subtitle[96];
      std::snprintf(subtitle, sizeof(subtitle), "%s", n == 1 || n == 2 ? tr(STR_BROWSE) : tr(STR_CATALOG_BOOKS_UNIT));
      if (n == 0 && catalog_->catalogSummaryValue().collections.availability == Availability::Ready) {
        const auto collections = catalog_->catalogSummaryValue().collections.value;
        if (collections == 1)
          std::snprintf(subtitle, sizeof(subtitle), tr(STR_CATALOG_COLLECTION_ONE_FORMAT), unsigned(collections));
        else
          std::snprintf(subtitle, sizeof(subtitle), tr(STR_CATALOG_COLLECTIONS_FORMAT), unsigned(collections));
      }
      if (n == 3 && catalog_->catalogSummaryValue().linked.availability == Availability::Ready &&
          catalog_->catalogSummaryValue().sourceLabel[0])
        std::snprintf(subtitle, sizeof(subtitle), tr(STR_CATALOG_SOURCE_FORMAT),
                      unsigned(catalog_->catalogSummaryValue().linked.value),
                      catalog_->catalogSummaryValue().sourceLabel.data());
      const int subtitleLine = renderer.getLineHeight(SMALL_FONT_ID);
      text(Rect{box.x + 8, box.y + tileH - subtitleLine - 5, box.width - 16, subtitleLine}, subtitle, SMALL_FONT_ID);
      hit(box, n + 10);
    }
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
    const auto s = safe();
    const int line = renderer.getLineHeight(UI_10_FONT_ID);
    const int top = headerBottom() + 62;
    if (catalog_->catalogPanel() == CatalogSurface::Panel::Browse) {
      const int h = (s.y + s.height - top - 60) / 5;
      text(Rect{s.x + 12, headerBottom() + 14, s.width - 24, line}, tr(STR_CATALOG_INDEXES));
      for (uint8_t i = 0; i < catalog_->catalogBrowseCount(); ++i) {
        const auto root = catalog_->catalogBrowseRootAt(i);
        const Rect tile{s.x + 12 + (i % 2) * ((s.width - 24) / 2), top + (i / 2) * h, (s.width - 34) / 2, h - 8};
        button(tile, rootTitle(root), i);
        number(Rect{tile.x + 9, tile.y + line + 17, tile.width - 40, line + 6}, catalog_->catalogRootCount(root));
        text(Rect{tile.x + tile.width - 25, tile.y + line + 17, 22, line}, "›");
      }
      return;
    }
    button(Rect{s.x + 12, headerBottom() + 6, 140, 50},
           catalog_->catalogState() == CatalogSurface::State::Error ||
                   catalog_->catalogState() == CatalogSurface::State::NotSaved ||
                   catalog_->catalogState() == CatalogSurface::State::Unsupported
               ? tr(STR_RETRY)
               : tr(STR_CATALOG_FILTER),
           catalog_->catalogVisibleRows());
    char label[96];
    if (catalog_->catalogState() != CatalogSurface::State::Ready &&
        catalog_->catalogState() != CatalogSurface::State::Empty) {
      text(Rect{s.x + 164, headerBottom() + 14, s.width - 176, line * 2},
           stateTitle(catalog_->catalogState(), catalog_->catalogPanel() == CatalogSurface::Panel::Sources));
    }
    if (catalog_->catalogState() == CatalogSurface::State::Ready ||
        catalog_->catalogState() == CatalogSurface::State::Empty || catalog_->catalogVisibleRows()) {
      if (catalog_->catalogPanel() == CatalogSurface::Panel::Sources)
        std::snprintf(label, sizeof(label), "%u %s", unsigned(catalog_->catalogTotal()),
                      catalog_->catalogRoot() == CatalogRoot::Lists
                          ? tr(STR_HOME_LISTS)
                          : (catalog_->catalogEntryTitle()[0] ? catalog_->catalogEntryTitle()
                                                              : rootTitle(catalog_->catalogRoot())));
      else if (!catalog_->catalogResult().membershipComplete)
        std::snprintf(label, sizeof(label), "%s", tr(STR_CATALOG_INCOMPLETE));
      else
        std::snprintf(label, sizeof(label), tr(STR_CATALOG_BOOKS_FORMAT), unsigned(catalog_->catalogTotal()));
      if (catalog_->catalogState() == CatalogSurface::State::Ready ||
          catalog_->catalogState() == CatalogSurface::State::Empty)
        text(Rect{s.x + 164, headerBottom() + 18, s.width - 176, line}, label);
      if (catalog_->catalogState() == CatalogSurface::State::Empty)
        text(Rect{s.x + 12, top + 15, s.width - 24, line * 2},
             stateTitle(catalog_->catalogState(), catalog_->catalogPanel() == CatalogSurface::Panel::Sources));
      const int h = (s.y + s.height - top - 112) / catalog_->catalogPageRows();
      for (uint8_t i = 0; i < catalog_->catalogVisibleRows(); ++i) {
        Rect row{s.x + 12, top + i * h, s.width - 24, h - 5};
        renderer.drawRect(
            row.x, row.y, row.width, row.height,
            (catalog_->catalogPanel() != CatalogSurface::Panel::Groups && catalog_->catalogFocus() == i) ? 3 : 1, true);
        hit(row, i);
        if (catalog_->catalogPanel() == CatalogSurface::Panel::Sources) {
          const auto& source = catalog_->catalogSourcesValue().rows[i];
          text(Rect{row.x + 9, row.y + 8, row.width - 130, line}, source.title.data());
          number(Rect{row.x + row.width - 120, row.y + 8, 110, line}, source.books);
          continue;
        }
        const auto& entry = catalog_->catalogResult().entries[i];
        if (catalog_->catalogPanel() == CatalogSurface::Panel::Groups) {
          text(Rect{row.x + 8, row.y + 5, row.width - 154, line},
               entry.missing ? (catalog_->catalogRoot() == CatalogRoot::Series ? tr(STR_CATALOG_NO_SERIES)
                                                                               : tr(STR_CATALOG_UNKNOWN_AUTHOR))
                             : entry.label.data());
          number(Rect{row.x + row.width - 142, row.y + 5, 134, line},
                 {entry.books,
                  catalog_->catalogResult().membershipComplete ? Availability::Ready : Availability::NotSaved},
                 entry.books > 1);
          // Header title opens Series; the right All action opens the scoped list.
          if (catalog_->catalogRoot() == CatalogRoot::Series && !entry.missing)
            hit(Rect{row.x, row.y, row.width - 150, 50}, i, 2);
          const Rect preview{row.x + 5, row.y + line + 14, row.width - 10, row.height - line - 20};
          if (catalog_->catalogFocus() == i)
            renderer.drawRect(preview.x, preview.y, preview.width, preview.height, 3, true);
          if (entry.books > 1 && catalog_->catalogResult().membershipComplete) {
            const Rect all{row.x + row.width - 146, row.y, 142, 50};
            hit(all, i, 4);
            if (catalog_->catalogFocus() == catalog_->catalogVisibleRows() + 3 + i)
              renderer.drawRect(all.x, all.y, all.width, all.height, 3, true);
          }
          const int inset = textOnly_ ? 10 : 64;
          if (!textOnly_)
            paintCatalogCover(Rect{preview.x + 5, preview.y + 5, 48, preview.height > 80 ? 72 : preview.height - 10},
                              entry.book, i);
          text(Rect{row.x + inset, preview.y + 8, row.width - inset - 8, line}, entry.book.title.data());
          catalogRowMetadata(label, entry,
                             entry.onDevice == QueryTruth::Yes  ? tr(STR_CATALOG_ON_CARD)
                             : entry.onDevice == QueryTruth::No ? tr(STR_CATALOG_SERVER_ONLY)
                                                                : tr(STR_HOME_NOT_SAVED));
          text(Rect{row.x + inset, preview.y + line + 12, row.width - inset - 8, line}, label);
        } else {
          const int inset = textOnly_ ? 8 : 48;
          if (!textOnly_) paintCatalogCover(Rect{row.x + 5, row.y + 4, 35, row.height - 8}, entry.book, i);
          text(Rect{row.x + inset, row.y + 5, row.width - inset - 50, line}, entry.book.title.data());
          const bool selected =
              entry.book.key == catalog_->catalogSelectedBook().key && catalog_->catalogSelectedBook().title[0];
          const char* detail = entry.onDevice == QueryTruth::Yes  ? tr(STR_CATALOG_ON_CARD)
                               : entry.onDevice == QueryTruth::No ? tr(STR_CATALOG_SERVER_ONLY)
                                                                  : tr(STR_HOME_NOT_SAVED);
          if (selected && catalog_->catalogAvailabilityUnknown())
            detail = tr(STR_HOME_NOT_SAVED);
          else if (selected && catalog_->catalogInlineError() != DownloadError::None)
            detail = catalog_->catalogChoices().availability == Availability::Unavailable
                         ? tr(STR_CATALOG_ACCESS)
                         : errorTitle(catalog_->catalogInlineError());
          else if (selected && catalog_->catalogPreparing())
            detail = tr(STR_CATALOG_PREPARING);
          else if (selected && catalog_->catalogTransferValue().state == DownloadState::Transferring)
            detail = tr(STR_DOWNLOADING);
          const int metadataFont = textOnly_ ? SMALL_FONT_ID : UI_10_FONT_ID;
          const int metadataLine = renderer.getLineHeight(metadataFont);
          const int metadataY = row.y + line + (textOnly_ ? 4 : 9);
          if (entry.label[0])
            text(Rect{row.x + inset, metadataY, row.width - inset - 50, metadataLine}, entry.label.data(),
                 metadataFont);
          const bool transferText =
              selected && (catalog_->catalogAvailabilityUnknown() || catalog_->catalogPreparing() ||
                           catalog_->catalogInlineError() != DownloadError::None ||
                           catalog_->catalogTransferValue().state == DownloadState::Transferring);
          catalogRowMetadata(label, entry, detail, transferText);
          detail = label;
          text(Rect{row.x + inset, metadataY + (entry.label[0] ? metadataLine + 4 : 0), row.width - inset - 50,
                    metadataLine},
               detail, metadataFont);
          text(Rect{row.x + row.width - 44, row.y + 8, 40, line}, "...");
          hit(Rect{row.x + row.width - 50, row.y, 50, row.height}, i, 1);
        }
      }
    }
    const int foot = s.y + s.height - 104;
    if (catalog_->catalogOffset()) button(Rect{s.x + 12, foot, 70, 50}, "<", catalog_->catalogVisibleRows() + 1);
    if (catalog_->catalogHasMore())
      button(Rect{s.x + s.width - 82, foot, 70, 50}, ">", catalog_->catalogVisibleRows() + 2);
    const auto pages = (catalog_->catalogTotal() + catalog_->catalogPageRows() - 1) / catalog_->catalogPageRows();
    if (catalog_->catalogPanel() == CatalogSurface::Panel::Groups)
      std::snprintf(label, sizeof(label), "%u / ?",
                    unsigned(catalog_->catalogOffset() / catalog_->catalogPageRows() + 1));
    else
      std::snprintf(label, sizeof(label), tr(STR_CATALOG_PAGE_FORMAT),
                    unsigned(catalog_->catalogOffset() / catalog_->catalogPageRows() + 1), unsigned(pages ? pages : 1));
    if (catalog_->catalogOffset() || catalog_->catalogHasMore())
      text(Rect{s.x + 92, foot + 12, s.width - 184, line}, label);
  }
  void paintCatalogDialog() const {
    const auto s = safe();
    const int line = renderer.getLineHeight(UI_10_FONT_ID);
    const bool choices = catalog_->catalogDialog() == CatalogSurface::Dialog::Choices;
    const int count = choices ? catalog_->catalogChoices().count + 1 : 2;
    const int height = 140 + count * 58;
    Rect box{s.x + 20, s.y + (s.height - height) / 2, s.width - 40, height};
    renderer.fillRect(box.x, box.y, box.width, box.height, false);
    renderer.drawRect(box.x, box.y, box.width, box.height, 2, true);
    text(Rect{box.x + 12, box.y + 10, box.width - 24, line * 2},
         catalog_->catalogDialog() == CatalogSurface::Dialog::Offline ? tr(STR_CATALOG_OFFLINE_PROMPT)
         : catalog_->catalogDialog() == CatalogSurface::Dialog::Login ? tr(STR_CATALOG_NO_LOGIN)
         : choices                                                    ? tr(STR_DOWNLOAD)
         : catalog_->catalogTransferValue().state == DownloadState::Failed ||
                 catalog_->catalogTransferValue().state == DownloadState::Canceled
             ? tr(STR_CATALOG_STOPPED)
             : tr(STR_DOWNLOADING));
    text(Rect{box.x + 12, box.y + 52, box.width - 24, line}, catalog_->catalogSelectedBook().title.data());
    char label[100];
    if (catalog_->catalogDialog() == CatalogSurface::Dialog::TransferInfo) {
      const auto& job = catalog_->catalogTransferValue();
      if (job.state == DownloadState::Failed)
        std::snprintf(label, sizeof(label), "%s",
                      job.error == DownloadError::Unavailable ? tr(STR_DOWNLOAD_FAILED) : errorTitle(job.error));
      else if (job.state == DownloadState::Canceled)
        std::snprintf(label, sizeof(label), "%s", tr(STR_CATALOG_CANCELED));
      else if (job.totalKnown)
        std::snprintf(label, sizeof(label), tr(STR_CATALOG_PROGRESS_FORMAT), (unsigned long long)job.received,
                      (unsigned long long)job.total);
      else
        std::snprintf(label, sizeof(label), tr(STR_CATALOG_BYTES_FORMAT), (unsigned long long)job.received);
      text(Rect{box.x + 12, box.y + 86, box.width - 24, line}, label);
    } else if (choices)
      text(Rect{box.x + 12, box.y + 86, box.width - 24, line}, catalog_->catalogChoices().destination[0]
                                                                   ? catalog_->catalogChoices().destination.data()
                                                                   : tr(STR_CATALOG_DESTINATION));
    hitCount_ = 0;  // The modal alone owns touch until Back/Cancel.
    for (uint8_t i = 0; i < count; ++i) {
      const char* title = tr(STR_BACK);
      if (choices)
        title = i < catalog_->catalogChoices().count ? catalog_->catalogChoices().rows[i].label.data() : tr(STR_CANCEL);
      else if (catalog_->catalogDialog() == CatalogSurface::Dialog::Offline)
        title = i ? tr(STR_CANCEL) : tr(STR_CATALOG_CONNECT);
      else if (catalog_->catalogDialog() == CatalogSurface::Dialog::Login)
        title = i ? tr(STR_CATALOG_SETUP) : tr(STR_CATALOG_KEEP_BROWSING);
      else if (!i)
        title = catalog_->catalogTransferValue().state == DownloadState::Failed ||
                        catalog_->catalogTransferValue().state == DownloadState::Paused ||
                        catalog_->catalogTransferValue().state == DownloadState::Canceled
                    ? tr(STR_CATALOG_RESUME)
                    : tr(STR_CANCEL);
      const Rect action{box.x + 12, box.y + 128 + i * 58, box.width - 24, 50};
      if (choices && i < catalog_->catalogChoices().count) {
        renderer.drawRect(action.x, action.y, action.width, action.height, catalog_->catalogFocus() == i ? 3 : 1, true);
        text(Rect{action.x + 9, action.y + 8, action.width / 2 - 12, line}, title);
        const auto& choice = catalog_->catalogChoices().rows[i];
        if (choice.bytesKnown)
          std::snprintf(label, sizeof(label), tr(STR_CATALOG_FILE_BYTES_FORMAT), (unsigned long long)choice.bytes);
        else
          std::snprintf(label, sizeof(label), "%s", tr(STR_CATALOG_SIZE_UNKNOWN));
        text(Rect{action.x + action.width / 2, action.y + 8, action.width / 2 - 8, line}, label);
        hit(action, i);
      } else
        button(action, title, i);
    }
  }

 public:
  CatalogActivity(GfxRenderer& renderer, MappedInputManager& input, Library& library, CatalogRoute entry)
      : Activity("Library", renderer, input), library_(library), entry_(entry) {}
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
    catalog_->enterCatalog(textOnly_, entry_);
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
              if (h.action == 1)
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
    text(Rect{s.x + 12, s.y + 8, 76, 44}, tr(STR_BACK));
    text(Rect{s.x + 92, s.y + 8, s.width - 104, 44},
         catalog_->catalogPanel() == CatalogSurface::Panel::Dashboard ? tr(STR_CATALOG_TITLE)
         : catalog_->catalogPanel() == CatalogSurface::Panel::Browse
             ? tr(STR_CATALOG_BROWSE_TITLE)
             : (catalog_->catalogEntryTitle()[0] ? catalog_->catalogEntryTitle() : rootTitle(catalog_->catalogRoot())),
         UI_12_FONT_ID);
    renderer.drawLine(s.x, headerBottom(), s.x + s.width, headerBottom());
    if (catalog_->catalogPanel() == CatalogSurface::Panel::Dashboard)
      dashboard();
    else
      list();
    if (catalog_->catalogDialog() != CatalogSurface::Dialog::None) paintCatalogDialog();
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_HOME_PREV), tr(STR_HOME_NEXT));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
};
}  // namespace
std::unique_ptr<Activity> createCatalogScreen(GfxRenderer& renderer, MappedInputManager& input, Library& library,
                                              CatalogRoute entry) {
  auto screen = makeUniqueNoThrow<CatalogActivity>(renderer, input, library, entry);
  if (!screen) LOG_ERR("BSC", "Cannot allocate catalog activity");
  return screen;
}
#ifdef SIMULATOR
void showCatalogScene(GfxRenderer& renderer, MappedInputManager& input, Library& library) {
  auto screen = createCatalogScreen(renderer, input, library);
  if (screen) activityManager.replaceActivity(std::move(screen));
}
uint32_t catalogScenePaints() { return paints; }
CatalogSurface* catalogSceneController() { return sceneController; }
#endif
}  // namespace booksync
