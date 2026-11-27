#include "Bootstrap.h"

#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <ScalableBuiltins.h>

#include <cstdio>

#include "CrossPointState.h"
#include "NetworkPhase.h"
#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "booksync/services/LocalLibrary.h"
#include "booksync/services/OfflineLibrary.h"
#include "booksync/ui/Home.h"
#include "booksync/ui/WidgetHost.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace booksync {
namespace {
class ShellActivity final : public Activity {
  Library& library;
  Home home;
  std::string bookPath;
  // One fallible <=6 KiB allocation per Home lifetime: the immutable visible
  // page is too large for the task stack and must be freed before reader entry.
  std::unique_ptr<WidgetHost> widgets;
  uint8_t focus = 0, focusRow = 0;
  bool lastAvailable = false;
  Rect tileRect(uint8_t index) const {
    const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const auto header = UITheme::getHeaderRect(renderer, UITheme::getInstance().getMetrics().headerHeight, safe);
    const auto& pg = widgets->page();
    const auto& p = pg.widgets[index];
    const int top = header.y + header.height + 16;
    const int height = safe.y + safe.height - top - 56;
    const int width = safe.width - 16;
    return Rect{safe.x + 8 + width * p.x / pg.cols, top + height * p.y / pg.rows, width * p.w / pg.cols - 8,
                height * p.h / pg.rows - 8};
  }
  WidgetBox widgetBox(uint8_t index) const {
    const auto r = tileRect(index);
    return {r.width, r.height, renderer.getLineHeight(UI_10_FONT_ID), widgets->page().textOnly, mappedInput.hasTouch()};
  }
  uint8_t focusRows(uint8_t index) const {
    if (!widgets || index >= widgets->page().count) return 1;
    const auto rows = widgets->widgetRows(index, widgetBox(index));
    return rows ? rows : 1;
  }
  const char* shelfTitle(WindowSource source) const {
    switch (source) {
      case WindowSource::UpNext:
        return tr(STR_HOME_UP_NEXT);
      case WindowSource::WantToRead:
        return tr(STR_HOME_WANT_TO_READ);
      case WindowSource::Unopened:
        return tr(STR_HOME_UNOPENED);
      case WindowSource::Random:
        return tr(STR_HOME_RANDOM);
      case WindowSource::FinishedYear:
        return tr(STR_HOME_FINISHED_YEAR);
      case WindowSource::PickBackUp:
        return tr(STR_HOME_PICK_BACK_UP);
      case WindowSource::OnDevice:
        return tr(STR_HOME_ON_DEVICE);
      default:
        return tr(STR_HOME_RECENT);
    }
  }
  const char* stateText(WidgetState state) const {
    switch (state) {
      case WidgetState::Loading:
        return tr(STR_HOME_LOADING);
      case WidgetState::Empty:
        return tr(STR_HOME_NO_BOOKS);
      case WidgetState::NotSaved:
        return tr(STR_HOME_NOT_SAVED);
      case WidgetState::Unsupported:
        return tr(STR_HOME_UNAVAILABLE);
      case WidgetState::Oversize:
        return tr(STR_HOME_WIDGET_ERROR);
      default:
        return tr(STR_HOME_WIDGET_ERROR);
    }
  }
  void paintFittedText(int font, int x, int y, const char* text, int width) const {
    char caption[96]{};
    size_t bytes = 0;
    while (bytes + 1 < sizeof(caption) && text[bytes]) {
      caption[bytes] = text[bytes];
      caption[++bytes] = 0;
      if ((static_cast<unsigned char>(text[bytes]) & 0xc0) == 0x80) continue;
      if (renderer.getTextWidth(font, caption) > width) {
        do {
          --bytes;
        } while (bytes && (static_cast<unsigned char>(caption[bytes]) & 0xc0) == 0x80);
        caption[bytes] = 0;
        break;
      }
    }
    renderer.drawText(font, x, y, caption);
  }
  void paintWidget(uint8_t index) const {
    const auto r = tileRect(index);
    if (r.width < 24 || r.height < 24) return;
    const auto& p = widgets->page().widgets[index];
    const auto view = widgets->view(index);
    const bool textOnly = widgets->page().textOnly;
    const bool heroTile = p.kind == WidgetKind::Continue && !textOnly;
    const bool selected = focus == index && !mappedInput.hasTouch();
    const bool hero = heroTile && !selected;
    const int font = UI_10_FONT_ID;
    const int line = renderer.getLineHeight(font);
    if (hero)
      renderer.fillRect(r.x, r.y, r.width, r.height);
    else if (!textOnly)
      renderer.drawRoundedRect(r.x, r.y, r.width, r.height, 1, 12, true);
    else
      renderer.drawLine(r.x, r.y, r.x + r.width, r.y);
    if (selected && !(p.kind == WidgetKind::Shelf && view.viewState == WidgetState::Known)) {
      renderer.drawRect(r.x + 3, r.y + 3, r.width - 6, r.height - 6, 3, !hero);
      renderer.drawRect(r.x + 8, r.y + 8, r.width - 16, r.height - 16, 1, !hero);
    }
    renderer.beginTextClip(r.x + 10, r.y + 8, r.width - 20, r.height - 16);
    const char* title = p.kind == WidgetKind::Continue ? tr(STR_CONTINUE_READING)
                        : p.kind == WidgetKind::Shelf  ? shelfTitle(p.source)
                                                       : tr(STR_HOME_BROWSE);
    renderer.drawText(font, r.x + 12, r.y + 10, title, !hero, EpdFontFamily::BOLD);
    int y = r.y + line + 22;
    if (view.viewState != WidgetState::Known) {
      renderer.drawText(font, r.x + 12, y, stateText(view.viewState), !hero);
      if ((view.viewState == WidgetState::Error || view.viewState == WidgetState::Oversize) &&
          y + 2 * line < r.y + r.height)
        renderer.drawText(font, r.x + 12, y + line + 8, tr(STR_RETRY), !hero);
    } else if (p.kind == WidgetKind::Browse) {
      char text[80];
      std::snprintf(text, sizeof(text), tr(STR_HOME_BOOK_COUNT_FORMAT), static_cast<unsigned long>(view.total));
      renderer.drawText(font, r.x + 12, y, text);
      // Root links use named Library intents; their surfaces land in F36.
      y += line + 8;
      renderer.drawText(font, r.x + 12, y, tr(STR_HOME_ON_DEVICE));
    } else if (p.kind == WidgetKind::Shelf) {
      WidgetCell cell;
      for (uint8_t row = 0; row < view.count && widgets->widgetCell(index, widgetBox(index), row, cell); ++row) {
        const int x = r.x + cell.x;
        const int cellY = r.y + cell.y;
        if (selected && focusRow == row) renderer.drawRect(x - 3, cellY - 3, cell.width + 6, cell.height + 6, 2, true);
        const auto& book = view.rows[row];
        if (cell.cover) {
          const int coverHeight = cell.height - line - 6;
          const int coverWidth = coverHeight * 2 / 3;
          renderer.drawRect(x, cellY, coverWidth < cell.width ? coverWidth : cell.width, coverHeight);
          char initial[2] = {book.title[0] >= 32 && book.title[0] < 127 ? book.title[0] : '?', 0};
          renderer.drawText(BITTER_12_FONT_ID, x + 8, cellY + 8, initial);
          paintFittedText(font, x, cellY + coverHeight + 6, book.title.data(), cell.width - 8);
        } else {
          int titleWidth = cell.width;
          if (cell.progress) {
            char text[48];
            std::snprintf(text, sizeof(text), tr(STR_HOME_PROGRESS_FORMAT), static_cast<unsigned long>(book.page),
                          static_cast<unsigned long>(book.pages));
            if (textOnly) {
              const int width = renderer.getTextWidth(font, text);
              titleWidth -= width + 12;
              renderer.drawText(font, x + cell.width - width, cellY, text);
            } else
              renderer.drawText(font, x, cellY + line + 12, text);
          }
          paintFittedText(font, x, cellY, book.title.data(), titleWidth);
        }
      }
    } else {
      const int coverWidth = heroTile && r.height >= 140 ? 66 : 0;
      const int textX = r.x + 12 + (coverWidth ? coverWidth + 16 : 0);
      if (coverWidth) {
        // No saved bitmap is required: the approved typographic cover placeholder
        // is drawn from immutable title bytes, never acquired during paint.
        renderer.drawRect(r.x + 12, y, coverWidth, r.height - (y - r.y) - 18, 2, !hero);
        char initial[2] = {view.rows[0].title[0] ? view.rows[0].title[0] : '?', 0};
        renderer.drawText(BITTER_16_FONT_ID, r.x + 30, y + 18, initial, !hero);
      }
      for (uint8_t row = 0; row < view.count && y + line < r.y + r.height - 8; ++row) {
        const auto& book = view.rows[row];
        renderer.drawText(heroTile ? BITTER_14_FONT_ID : font, textX, y,
                          book.title[0] ? book.title.data() : tr(STR_CONTINUE_READING), !hero);
        y += line + 12;
        if (book.readingKnown && book.pages && y + line < r.y + r.height - 8) {
          char text[48];
          std::snprintf(text, sizeof(text), tr(STR_HOME_PROGRESS_FORMAT), static_cast<unsigned long>(book.page),
                        static_cast<unsigned long>(book.pages));
          renderer.drawText(font, textX, y, text, !hero);
          y += line + 8;
        }
      }
      if (heroTile && r.height > 100)
        renderer.drawText(font, textX, r.y + r.height - line - 14, tr(STR_OPEN), !hero, EpdFontFamily::BOLD);
    }
    renderer.endTextClip();
  }
  HalDisplay::RefreshMode initialRefresh;

 public:
  ShellActivity(GfxRenderer& renderer, MappedInputManager& input, HalDisplay::RefreshMode refresh,
                Library& library = offlineLibrary())
      : Activity("Home", renderer, input), library(library), home(library), initialRefresh(refresh) {}
  bool isHomeActivity() const override { return true; }
  void onEnter() override {
    Activity::onEnter();
#if CROSSINK_SCALABLE_FONTS
    // Reuse the reader's fallible built-in serif loader on S3. It prepares the
    // existing font family once, outside paint, and supplies its UI fallback on
    // allocation failure. C3 keeps its flash-resident bitmap family.
    ensureScalableBuiltinFamily(renderer, 1);
#endif
    enterLocalLibrary();
    setNetworkReading(!installNetworkMemory());
    bookPath = APP_STATE.openEpubPath;
    if (bookPath.empty()) {
      const auto& books = RECENT_BOOKS.getBooks();
      if (!books.empty()) bookPath = books.front().path;
    }
    // Entry-time validation only; no scans, writes or file I/O during paint.
    lastAvailable = !bookPath.empty() && Storage.exists(bookPath.c_str());
    const char* title = nullptr;
    for (const auto& book : RECENT_BOOKS.getBooks())
      if (book.path == bookPath) {
        title = book.title.c_str();
        break;
      }
    publishHomeBook(lastAvailable, title);
    widgets = makeUniqueNoThrow<WidgetHost>(library);
    if (widgets)
      widgets->enter();
    else
      LOG_ERR("BSH", "Cannot allocate widget host (%u bytes)", unsigned(sizeof(WidgetHost)));
    requestUpdate();
  }
  void onExit() override {
    setNetworkReading(true);
    widgets.reset();
    leaveLocalLibrary();
    publishHomeBook(false);
    Activity::onExit();
  }
  void loop() override {
    if (!networkDisplayReady()) return;
    // Keep the local index's SD work outside the admitted network worker phase.
    if (!networkAdmission().busy()) {
      RenderLock lock(RenderLock::Mode::Try);
      if (lock.ownsLock()) workLocalLibrary();
    }
    {
      RenderLock lock;
      if (widgets && widgets->pollWidgets()) {
        if (focus >= widgets->page().count) focus = 0;
        if (focusRow >= focusRows(focus)) focusRow = 0;
        requestUpdate();
      }
      if (widgets && widgets->page().count) {
        const auto count = widgets->page().count;
        int step = 0;
        if (mappedInput.wasReleased(mappedInput.menuButton(MappedInputManager::Button::Down))) step = 1;
        if (mappedInput.wasReleased(mappedInput.menuButton(MappedInputManager::Button::Up))) step = -1;
        if (step) {
          if (step > 0 && focusRow + 1 < focusRows(focus))
            ++focusRow;
          else if (step < 0 && focusRow)
            --focusRow;
          else {
            const int next = focus + step;
            if (next >= 0 && next < count) {
              focus = next;
              focusRow = step < 0 ? focusRows(focus) - 1 : 0;
            } else {
              const auto& page = widgets->page();
              const int nextPage = page.page + step;
              if (nextPage >= 0 && nextPage < page.pages) {
                widgets->enter(nextPage);
                focus = focusRow = 0;
              }
            }
          }
          requestUpdate();
        }
        if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
          if (!widgets->retryWidget(focus) && focusRow < widgets->widgetRows(focus, widgetBox(focus)))
            widgets->dispatchWidget(focus, focusRow);
        }
        int x = 0, y = 0;
        if (mappedInput.wasScreenTapped(x, y)) {
          for (uint8_t i = 0; i < count; ++i) {
            const auto r = tileRect(i);
            if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height) {
              if (!widgets->retryWidget(i)) {
                const auto row = widgets->hitWidgetRow(i, widgetBox(i), x - r.x, y - r.y);
                if (row != 255) widgets->dispatchWidget(i, row);
              }
            }
          }
          const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
          if (y >= safe.y + safe.height - 56 && y < safe.y + safe.height && x >= safe.x && x < safe.x + safe.width) {
            const auto& page = widgets->page();
            const int next = page.page + (x < safe.x + safe.width / 2 ? -1 : 1);
            if (next >= 0 && next < page.pages) {
              widgets->enter(next);
              focus = focusRow = 0;
              requestUpdate();
            }
          }
        }
      } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm))
        home.submit();
    }
    std::string indexedPath;
    if (takeIndexedOpenRequest(indexedPath)) activityManager.goToReader(indexedPath);
    if (takeHomeOpenRequest()) activityManager.goToReader(bookPath);
  }
  void render(RenderLock&&) override {
    renderer.clearScreen();
    const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const auto header = UITheme::getHeaderRect(renderer, UITheme::getInstance().getMetrics().headerHeight, safe);
    const char* title = widgets && widgets->page().title[0] ? widgets->page().title.data() : tr(STR_HOME);
    renderer.beginTextClip(header.x + 12, header.y, header.width - 24, header.height);
    renderer.drawText(BITTER_14_FONT_ID, header.x + 12, header.y + 4, title, true, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, header.x + 12, header.y + renderer.getLineHeight(BITTER_14_FONT_ID) + 6,
                      tr(STR_HOME_OFFLINE));
    renderer.endTextClip();
    if (widgets && widgets->page().revision) {
      for (uint8_t i = 0; i < widgets->page().count; ++i) paintWidget(i);
      char text[24];
      std::snprintf(text, sizeof(text), tr(STR_HOME_PAGE_FORMAT), widgets->page().page + 1, widgets->page().pages);
      renderer.drawText(UI_10_FONT_ID, safe.x + 12, safe.y + safe.height - 30, text);
    } else {
      UITheme::drawCenteredText(renderer, safe, UI_12_FONT_ID, header.y + header.height + 24,
                                lastAvailable ? tr(STR_CONTINUE_READING) : tr(STR_NO_RECENT_BOOKS));
    }
    const char* action = lastAvailable && (!widgets || !widgets->page().revision) ? tr(STR_OPEN) : "";
    if (widgets && focus < widgets->page().count) {
      const auto state = widgets->view(focus).viewState;
      if (state == WidgetState::Known && focusRow < widgets->widgetRows(focus, widgetBox(focus)))
        action = widgets->page().widgets[focus].kind == WidgetKind::Browse ? tr(STR_HOME_BROWSE) : tr(STR_OPEN);
      else if (state == WidgetState::Error || state == WidgetState::Oversize)
        action = tr(STR_RETRY);
    }
    const auto labels = mappedInput.mapLabels("", action, tr(STR_HOME_PREV), tr(STR_HOME_NEXT));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(initialRefresh);
    initialRefresh = HalDisplay::FAST_REFRESH;
  }
};
}  // namespace
Library& homeLibrary() { return offlineLibrary(); }
void showHome(GfxRenderer& renderer, MappedInputManager& input, HalDisplay::RefreshMode refresh) {
  // Activity lifetime must outlive this call and is owned by ActivityManager.
  auto shell = makeUniqueNoThrow<ShellActivity>(renderer, input, refresh);
  if (!shell) {
    LOG_ERR("BOOKSYNC", "Cannot allocate Home shell");
    return;
  }
  activityManager.replaceActivity(std::move(shell));
}
#ifdef SIMULATOR
void showHomeScene(GfxRenderer& renderer, MappedInputManager& input, Library& library) {
  auto shell = makeUniqueNoThrow<ShellActivity>(renderer, input, HalDisplay::FULL_REFRESH, library);
  if (!shell) {
    LOG_ERR("BSH", "Cannot allocate Home scene");
    return;
  }
  activityManager.replaceActivity(std::move(shell));
}
#endif
}  // namespace booksync
