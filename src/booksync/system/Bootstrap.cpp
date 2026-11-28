#include "Bootstrap.h"

#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <ScalableBuiltins.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "CrossPointState.h"
#include "booksync/import/Activation.h"
#include "booksync/services/ServerAdapter.h"
#ifndef SIMULATOR
#include <esp_mac.h>
#endif
#include "NetworkPhase.h"
#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "booksync/services/LocalLibrary.h"
#include "booksync/services/OfflineLibrary.h"
#include "booksync/ui/EditorAddRemove.h"
#include "booksync/ui/Home.h"
#include "booksync/ui/WidgetHost.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace booksync {
namespace {
#ifdef SIMULATOR
uint32_t scenePaints = 0;  // Read only while holding RenderLock in the scenario harness.
#endif
legacy::Activation upgrade;
// Independent connector scopes: two sync logins and up to eight acquisition
// accounts. No secrets or network clients are retained in these small values.
Account syncAccount(defaultServerAdapter()), nativeAccount(defaultServerAdapter());
Account acquisitionAccounts[8] = {Account(defaultServerAdapter()), Account(defaultServerAdapter()),
                                  Account(defaultServerAdapter()), Account(defaultServerAdapter()),
                                  Account(defaultServerAdapter()), Account(defaultServerAdapter()),
                                  Account(defaultServerAdapter()), Account(defaultServerAdapter())};
std::atomic<bool> identityAdmitted{false}, upgradeAdmitted{false};
bool noticePending = false, recoveryRequired = false, priorReaderState = false;
class ShellActivity final : public Activity {
  Library& library;
  Home home;
#if CROSSINK_APP_CAP_HOME_EDITOR
  EditorAddRemove editor;
#endif
  std::string bookPath;
  // One fallible <=6 KiB allocation per Home lifetime: the immutable visible
  // page is too large for the task stack and must be freed before reader entry.
  std::unique_ptr<WidgetHost> widgets;
  uint8_t focus = 0, focusRow = 0;
  bool lastAvailable = false;
  Rect tileRect(uint8_t index) const {
    const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const auto header = UITheme::getHeaderRect(renderer, UITheme::getInstance().getMetrics().headerHeight, safe);
    const auto& pg =
#if CROSSINK_APP_CAP_HOME_EDITOR
        editor.draftPage() ? *editor.draftPage() :
#endif
                           widgets->page();
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
#if CROSSINK_APP_CAP_HOME_EDITOR
    const bool editing = editor.panel() != EditorAddRemove::Panel::Closed;
    const bool selected = editing ? (editor.panel() == EditorAddRemove::Panel::Selected ? editor.selected() == index
                                                                                        : editor.focus() == index)
                                  : focus == index && !mappedInput.hasTouch();
#else
    const bool selected = focus == index && !mappedInput.hasTouch();
#endif
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

#if CROSSINK_APP_CAP_HOME_EDITOR
  bool canEdit() const { return widgets && widgets->page().revision && !widgets->page().textOnly; }
  const char* editorLabel(uint8_t row) const {
    using Panel = EditorAddRemove::Panel;
    const auto panel = editor.panel();
    if (panel == Panel::Selected || panel == Panel::Options)
      return row == 0 ? tr(STR_HOME_REMOVE_WIDGET) : tr(STR_DONE);
    if (panel == Panel::Reset) return row == 0 ? tr(STR_HOME_KEEP_LAYOUT) : tr(STR_HOME_RESET_LAYOUT);
    if (panel == Panel::Error) return tr(STR_BACK);
    if (panel == Panel::Add)
      return row == 0 ? tr(STR_CONTINUE_READING) : row == 1 ? tr(STR_HOME_SHELF) : tr(STR_HOME_BROWSE);
    const auto count = widgets->page().count;
    return row == count ? tr(STR_HOME_ADD_WIDGET) : row == count + 1 ? tr(STR_HOME_RESET_LAYOUT) : tr(STR_DONE);
  }
  Rect editorHeaderButton(uint8_t index) const {
    const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const auto header = UITheme::getHeaderRect(renderer, UITheme::getInstance().getMetrics().headerHeight, safe);
    const int width = (header.width - 24) / (editor.panel() == EditorAddRemove::Panel::Selected ? 2 : 3);
    const int height = renderer.getLineHeight(UI_10_FONT_ID) + 12;
    return Rect{header.x + 12 + index * width, header.y + renderer.getLineHeight(BITTER_14_FONT_ID) + 8, width - 4,
                height < 50 ? 50 : height};
  }
  Rect editorRow(uint8_t index) const {
    const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const auto header = UITheme::getHeaderRect(renderer, UITheme::getInstance().getMetrics().headerHeight, safe);
    const int line = renderer.getLineHeight(UI_10_FONT_ID);
    const int height = line + 26;
    return Rect{safe.x + 24, header.y + header.height + line * 3 + 28 + index * height, safe.width - 48, height};
  }
  Rect editorMoveButton(uint8_t index) const {
    const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const int width = (safe.width - 16) / 7;
    return Rect{safe.x + 8 + index * width, safe.y + safe.height - 54,
                (index == 4 ? safe.width - 16 - index * width : width) - 2, 52};
  }
  void paintEditorMoveBar() const {
    if (mappedInput.hasTouch()) {
      for (uint8_t i = 0; i < 5; ++i) {
        const auto r = editorMoveButton(i);
        renderer.drawRect(r.x, r.y, r.width, r.height, 1, true);
        if (i < 4) {
          const int dx = i == 0 ? -1 : i == 3 ? 1 : 0;
          const int dy = i == 1 ? -1 : i == 2 ? 1 : 0;
          const int cx = r.x + r.width / 2, cy = r.y + r.height / 2;
          renderer.drawLine(cx - dx * 10, cy - dy * 10, cx + dx * 10, cy + dy * 10, 2, true);
          renderer.drawLine(cx + dx * 10, cy + dy * 10, cx + dx * 3 + dy * 7, cy + dy * 3 + dx * 7, 2, true);
          renderer.drawLine(cx + dx * 10, cy + dy * 10, cx + dx * 3 - dy * 7, cy + dy * 3 - dx * 7, 2, true);
        } else {
          char label[48];
          const auto& tile = editor.draftPage()->widgets[editor.selected()];
          std::snprintf(label, sizeof(label), tr(STR_HOME_EDIT_NEXT_SIZE_FORMAT), tile.w, tile.h, editor.nextWidth(),
                        editor.nextHeight());
          paintFittedText(UI_10_FONT_ID, r.x + 5, r.y + 14, editor.nextWidth() ? label : tr(STR_HOME_EDIT_BLOCKED),
                          r.width - 10);
        }
      }
    } else {
      const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
      renderer.fillRect(safe.x + 8, safe.y + safe.height - 54, safe.width - 16, 52, true);
      renderer.drawText(UI_10_FONT_ID, safe.x + 16, safe.y + safe.height - 52, tr(STR_HOME_EDIT_CARRY_MOVE), false);
      renderer.drawText(UI_10_FONT_ID, safe.x + 16, safe.y + safe.height - 28, tr(STR_HOME_EDIT_CARRY_DROP), false);
    }
  }
  void paintEditor() const {
    using Panel = EditorAddRemove::Panel;
    const auto panel = editor.panel();
    if (panel == Panel::Select || panel == Panel::Selected) {
      // Reuse the Home tiles and snapped grid; no second framebuffer.
      for (uint8_t i = 0; i < widgets->page().count; ++i) paintWidget(i);
      for (uint8_t i = 0; i < (panel == Panel::Selected ? 2 : 3); ++i) {
        const auto r = editorHeaderButton(i);
        const auto row = panel == Panel::Selected ? i : widgets->page().count + i;
        renderer.drawRect(r.x, r.y, r.width, r.height, panel == Panel::Select && editor.focus() == row ? 3 : 1, true);
        paintFittedText(UI_10_FONT_ID, r.x + 5, r.y + 4, editorLabel(row), r.width - 10);
      }
      if (panel == Panel::Selected) {
        paintEditorMoveBar();
        const auto& tile = editor.draftPage()->widgets[editor.selected()];
        char size[32];
        std::snprintf(size, sizeof(size), tr(STR_HOME_EDIT_SIZE_FORMAT), tile.w, tile.h);
        const auto box = tileRect(editor.selected());
        const char* tag = editor.blocked() ? tr(STR_HOME_EDIT_BLOCKED) : size;
        const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, tag);
        const int tagWidth = textWidth + 12 < box.width - 12 ? textWidth + 12 : box.width - 12;
        const int tagX = box.x + box.width - tagWidth - 4;
        renderer.fillRect(tagX, box.y + box.height - 28, tagWidth, 26, false);
        renderer.drawRect(tagX, box.y + box.height - 28, tagWidth, 26, 1, true);
        paintFittedText(UI_10_FONT_ID, tagX + 6, box.y + box.height - 26, tag, tagWidth - 12);
      }
      return;
    }
    const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const auto header = UITheme::getHeaderRect(renderer, UITheme::getInstance().getMetrics().headerHeight, safe);
    const char* title = panel == Panel::Add                                   ? tr(STR_HOME_ADD_WIDGET)
                        : panel == Panel::Reset                               ? tr(STR_HOME_RESET_CONFIRM)
                        : panel == Panel::Waiting                             ? tr(STR_HOME_EDIT_SAVING)
                        : panel == Panel::Options                             ? tr(STR_HOME_REMOVE_WIDGET)
                        : editor.editorFailure() == LayoutEditStatus::NoSpace ? tr(STR_HOME_EDIT_NO_SPACE)
                        : editor.editorFailure() == LayoutEditStatus::Stale   ? tr(STR_HOME_EDIT_STALE)
                                                                              : tr(STR_HOME_EDIT_FAILED);
    // Fixed line buffer; translated prompts wrap above the action rows.
    char lineText[128]{};
    unsigned offset = 0;
    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    for (unsigned row = 0; row < 3 && title[offset]; ++row) {
      unsigned n = 0, word = 0;
      while (title[offset + n] && n + 1 < sizeof(lineText)) {
        lineText[n] = title[offset + n];
        lineText[++n] = 0;
        if (title[offset + n - 1] == ' ') word = n;
        if ((static_cast<unsigned char>(title[offset + n]) & 0xc0) == 0x80) continue;
        if (renderer.getTextWidth(UI_10_FONT_ID, lineText) > safe.width - 48) {
          if (word)
            n = word;
          else
            do {
              --n;
            } while (n && (static_cast<unsigned char>(lineText[n]) & 0xc0) == 0x80);
          break;
        }
      }
      lineText[n] = 0;
      renderer.drawText(UI_10_FONT_ID, safe.x + 24, header.y + header.height + 20 + row * lineHeight, lineText);
      offset += n;
      while (title[offset] == ' ') ++offset;
    }
    if (panel == Panel::Waiting) return;
    for (uint8_t i = 0; i < editor.rows(); ++i) {
      const auto r = editorRow(i);
      renderer.drawRect(r.x, r.y, r.width, r.height, i == editor.focus() ? 3 : 1, true);
      paintFittedText(UI_10_FONT_ID, r.x + 12, r.y + 10, editorLabel(i), r.width - 24);
    }
  }
  bool editorInput() {
    using Panel = EditorAddRemove::Panel;
    if (!canEdit()) return false;
    const bool active = editor.panel() != Panel::Closed;
    int x = 0, y = 0;
    const bool tapped = mappedInput.wasScreenTapped(x, y);
    if (!active) {
      // The approved Home menu entry is reached with More / header tap.
      const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
      const auto header = UITheme::getHeaderRect(renderer, UITheme::getInstance().getMetrics().headerHeight, safe);
      if (mappedInput.wasReleased(mappedInput.menuButton(MappedInputManager::Button::Right)) ||
          (tapped && x >= header.x + header.width - 80 && y >= header.y && y < header.y + header.height)) {
        editor.enter();
        requestUpdate();
        return true;
      }
      return false;
    }
    const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const auto header = UITheme::getHeaderRect(renderer, UITheme::getInstance().getMetrics().headerHeight, safe);
    const bool touchBack = tapped && editor.panel() != Panel::Select && editor.panel() != Panel::Selected &&
                           x >= header.x + header.width - 80 && x < header.x + header.width && y >= header.y &&
                           y < header.y + header.height;
    if (touchBack || mappedInput.wasReleased(MappedInputManager::Button::Back))
      editor.back();
    else if (mappedInput.wasReleased(mappedInput.menuButton(MappedInputManager::Button::Right)))
      editor.moreEditor();
    else if (mappedInput.wasReleased(mappedInput.menuButton(MappedInputManager::Button::Down)))
      editor.stepEditor(1);
    else if (mappedInput.wasReleased(mappedInput.menuButton(MappedInputManager::Button::Up)))
      editor.stepEditor(-1);
    else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm))
      editor.activateEditor();
    else if (tapped) {
      if (editor.panel() == Panel::Select || editor.panel() == Panel::Selected) {
        if (editor.panel() == Panel::Selected) {
          static constexpr LayoutStep steps[] = {LayoutStep::Left, LayoutStep::Up, LayoutStep::Down, LayoutStep::Right,
                                                 LayoutStep::Size};
          for (uint8_t i = 0; i < 5; ++i) {
            const auto r = editorMoveButton(i);
            if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height) {
              editor.geometryStep(steps[i]);
              requestUpdate();
              return true;
            }
          }
        }
        const bool selected = editor.panel() == Panel::Selected;
        for (uint8_t i = 0; i < (selected ? 2 : 3); ++i) {
          const auto r = editorHeaderButton(i);
          if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height) {
            editor.chooseEditor(selected ? i : widgets->page().count + i);
            break;
          }
        }
        if (editor.panel() == Panel::Select)
          for (uint8_t i = 0; i < widgets->page().count; ++i) {
            const auto r = tileRect(i);
            if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height) {
              editor.chooseEditor(i);
              break;
            }
          }
      } else
        for (uint8_t i = 0; i < editor.rows(); ++i) {
          const auto r = editorRow(i);
          if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height) {
            editor.chooseEditor(i);
            break;
          }
        }
    } else {
      return true;  // Consume the editor context without scheduling an idle repaint.
    }
    requestUpdate();
    return true;
  }
#else
  bool editorInput() { return false; }
#endif
 public:
  ShellActivity(GfxRenderer& renderer, MappedInputManager& input, HalDisplay::RefreshMode refresh,
                Library& library = offlineLibrary())
      : Activity("Home", renderer, input),
        library(library),
        home(library),
#if CROSSINK_APP_CAP_HOME_EDITOR
        editor(library),
#endif
        initialRefresh(refresh) {
  }
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
#if CROSSINK_APP_CAP_HOME_EDITOR
      if (editor.pollEditor()) {
        if (widgets) {
          const auto page = library.homePage().page;
          // The host already batches a changed revision on this page. Only a
          // reset from another page needs a new page request.
          if (widgets->page().page != page) {
            widgets->enter(page);
            widgets->pollWidgets();  // Adopt the reset page before the next draw.
          }
        }
        requestUpdate();
      }
#endif
      if (editorInput()) return;
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
#ifdef SIMULATOR
    ++scenePaints;
#endif
    renderer.clearScreen();
    const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const auto header = UITheme::getHeaderRect(renderer, UITheme::getInstance().getMetrics().headerHeight, safe);
#if CROSSINK_APP_CAP_HOME_EDITOR
    const bool editing = editor.panel() != EditorAddRemove::Panel::Closed;
#else
    constexpr bool editing = false;
#endif
    const char* title = editing                               ? tr(STR_HOME_EDIT)
                        : widgets && widgets->page().title[0] ? widgets->page().title.data()
                                                              : tr(STR_HOME);
#if CROSSINK_APP_CAP_HOME_EDITOR
    if (editing && editor.draftPage()) {
      const auto kind = editor.draftPage()->widgets[editor.selected()].kind;
      title = kind == WidgetKind::Continue ? tr(STR_CONTINUE_READING)
              : kind == WidgetKind::Shelf  ? tr(STR_HOME_SHELF)
                                           : tr(STR_HOME_BROWSE);
    }
#endif
    int titleWidth = header.width - 24;
#if CROSSINK_APP_CAP_HOME_EDITOR
    if (canEdit()) titleWidth -= 80;
#endif
    renderer.beginTextClip(header.x + 12, header.y, titleWidth, header.height);
    renderer.drawText(BITTER_14_FONT_ID, header.x + 12, header.y + 4, title, true, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, header.x + 12, header.y + renderer.getLineHeight(BITTER_14_FONT_ID) + 6,
                      editing ? "" : tr(STR_HOME_OFFLINE));
    renderer.endTextClip();
#if CROSSINK_APP_CAP_HOME_EDITOR
    if (editing) {
      paintEditor();
    } else
#endif
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
#if CROSSINK_APP_CAP_HOME_EDITOR
    if (canEdit() && !editing) {
      paintFittedText(UI_10_FONT_ID, header.x + header.width - 72, header.y + 4, tr(STR_EDIT), 68);
    } else if (editing && editor.panel() != EditorAddRemove::Panel::Select &&
               editor.panel() != EditorAddRemove::Panel::Selected) {
      paintFittedText(UI_10_FONT_ID, header.x + header.width - 72, header.y + 4, tr(STR_BACK), 68);
    }
#endif
    const auto labels = mappedInput.mapLabels(editing ? tr(STR_BACK) : "", editing ? tr(STR_SELECT) : action,
                                              tr(STR_HOME_PREV), tr(STR_HOME_NEXT));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(initialRefresh);
    initialRefresh = HalDisplay::FAST_REFRESH;
  }
};
}  // namespace
void prepareUpgradeDetection() { priorReaderState = legacy::Activation::readerStatePresent(); }
void initializeUpgrade() {
  uint8_t hardwareKey[6]{};
#ifdef SIMULATOR
  hardwareKey[0] = 2;
  hardwareKey[5] = 1;  // Public synthetic fixture identity only.
  import::IdentityAudit identity;
  identity.status = import::IdentityStatus::Absent;
  if (std::getenv("BOOKSYNC_ACTIVATION_IDENTITY")) {
    identity.status = import::IdentityStatus::RetainedInPlace;
    identity.identityBlobs = 6;
    identity.joined = true;
    identity.fingerprint.fill(7);
    if (std::strcmp(std::getenv("BOOKSYNC_ACTIVATION_IDENTITY"), "changed") == 0) identity.fingerprint[0] ^= 1;
    if (std::strcmp(std::getenv("BOOKSYNC_ACTIVATION_IDENTITY"), "unreadable") == 0)
      identity.status = import::IdentityStatus::ReadError;
  }
#else
  if (esp_efuse_mac_get_default(hardwareKey) != ESP_OK) {
    LOG_ERR("BSA", "Hardware credential key unavailable; no import attempted");
    recoveryRequired = true;
    publishUpgradeState(false, true, dismissUpgradeNotice);
    return;
  }
  const auto identity = import::auditIdentity();
#endif
  // One reusable fallible <=20 KiB workspace only during boot, before Home or
  // reader allocations. Its destructor erases decoded credentials and the key.
  legacy::Credentials credentials(hardwareKey);
  eraseSecret(hardwareKey, sizeof(hardwareKey));
  bool restoreUnresolved = false;
  if (upgrade.boot(credentials, identity, priorReaderState) == legacy::ActivationResult::Active) {
    auto restore = [&](legacy::CredentialFamily family, Account& account, size_t index = 0) {
      if (upgrade.hasLogin(family, index) &&
          upgrade.restore(credentials, family, account, index) != legacy::ImportResult::Restored)
        restoreUnresolved = true;
    };
    restore(legacy::CredentialFamily::LibrarySync, syncAccount);
    restore(legacy::CredentialFamily::NativeSync, nativeAccount);
    for (size_t i = 0; i < upgrade.acquisitionCount(); ++i)
      restore(legacy::CredentialFamily::Acquisition, acquisitionAccounts[i], i);
  }
  upgradeAdmitted.store(upgrade.ready(), std::memory_order_release);
  identityAdmitted.store(upgrade.identityReady(), std::memory_order_release);
  noticePending = upgrade.noticePending();
  recoveryRequired = upgrade.recoveryRequired() || restoreUnresolved;
  publishUpgradeState(noticePending, recoveryRequired, dismissUpgradeNotice);
}
AccountSnapshot retainedLoginSnapshot(unsigned scope) {
  if (scope == 0) return syncAccount.snapshot();
  if (scope == 1) return nativeAccount.snapshot();
  return scope < 10 ? acquisitionAccounts[scope - 2].snapshot() : AccountSnapshot{};
}
AuthError removeRetainedLogin(unsigned scope) {
  if (scope >= 10) return AuthError::Invalid;
  auto& account = scope == 0 ? syncAccount : scope == 1 ? nativeAccount : acquisitionAccounts[scope - 2];
  const auto family = scope == 0   ? legacy::CredentialFamily::LibrarySync
                      : scope == 1 ? legacy::CredentialFamily::NativeSync
                                   : legacy::CredentialFamily::Acquisition;
  if (!upgrade.remove(family, scope < 2 ? 0 : scope - 2)) return AuthError::Unavailable;
  return account.removeLogin(scope < 2 ? LoginKind::Sync : LoginKind::Acquisition, account.snapshot().ticket);
}
bool retainedUpgradeReady() { return upgradeAdmitted.load(std::memory_order_acquire); }
bool retainedIdentityReady() { return identityAdmitted.load(std::memory_order_acquire); }
bool upgradeNoticePending() { return noticePending; }
bool upgradeRecoveryRequired() { return recoveryRequired; }
bool dismissUpgradeNotice() {
  const bool committed = upgrade.dismissNotice();
  noticePending = noticePending && upgrade.noticePending();
  publishUpgradeState(noticePending, recoveryRequired, dismissUpgradeNotice);
  return committed;
}
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
uint32_t homeScenePaints() { return scenePaints; }
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
