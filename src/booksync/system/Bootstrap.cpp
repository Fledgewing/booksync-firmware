#include "Bootstrap.h"

#include <FreeInkUIGfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <ScalableBuiltins.h>

#include <algorithm>
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
#include "ReadingGate.h"
#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "booksync/services/Layout.h"
#include "booksync/services/LocalLibrary.h"
#include "booksync/services/OfflineLibrary.h"
#include "booksync/ui/EditorAddRemove.h"
#include "booksync/ui/Home.h"
#include "booksync/ui/WidgetHost.h"
#include "booksync/ui/widgets/Clock.h"
#include "booksync/ui/widgets/Moon.h"
#include "booksync/ui/widgets/Glyph.h"
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
// Serialized boot/main-loop publication only. Future asynchronous auth must
// hand its result to this main-loop publisher before a presentation can adopt it.
void publishRetainedAccountView() {
  const auto account = syncAccount.snapshot();
  AccountView value;
  value.ticket = account.ticket;
  value.signedIn = account.signedIn;
  value.accountSupported = account.capabilities.account;
  value.syncSupported = account.capabilities.sync;
  value.catalogSupported = account.capabilities.acquisition;
  value.syncReady = account.syncReady;
  value.catalogReady = account.acquisitionReady;
  value.service = 1;
  value.available = true;
  const auto* label = defaultServerAdapter().serviceLabel();
  if (label) std::snprintf(value.serviceLabel.data(), value.serviceLabel.size(), "%s", label);
  publishAccountView(value);
}
std::atomic<bool> identityAdmitted{false}, upgradeAdmitted{false};
bool noticePending = false, recoveryRequired = false, priorReaderState = false;
#if CROSSINK_APP_CAP_HOME_EDITOR_SETTINGS
// The approved X3 Settings entry owns only the available Home widgets action.
// Do not route through the retired native Settings registry/action graph.
class HomeSettingsActivity final : public Activity {
 public:
  HomeSettingsActivity(GfxRenderer& renderer, MappedInputManager& input) : Activity("Settings", renderer, input) {}
  void onEnter() override {
    Activity::onEnter();
    requestUpdate();
  }
  void loop() override {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      activityManager.goHome();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      auto editor = createHomeEditor(renderer, mappedInput);
      if (editor) startActivityForResult(std::move(editor), [](const ActivityResult&) {});
    }
  }
  void render(RenderLock&&) override {
    // Static navigation only: no Library reads, work, scans or native settings.
    renderer.clearScreen();
    const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const auto header = UITheme::getHeaderRect(renderer, UITheme::getInstance().getMetrics().headerHeight, safe);
    const int line = renderer.getLineHeight(UI_10_FONT_ID);
    renderer.drawText(BITTER_14_FONT_ID, header.x + 12, header.y + 4, tr(STR_SETTINGS_TITLE));
    renderer.drawText(UI_10_FONT_ID, safe.x + 24, header.y + header.height + 16, tr(STR_CAT_DISPLAY));
    const int top = header.y + header.height + line + 32;
    renderer.fillRect(safe.x + 24, top, safe.width - 48, line + 18);
    renderer.drawText(UI_10_FONT_ID, safe.x + 36, top + 8, tr(STR_HOME_WIDGETS), false);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
};
static_assert(sizeof(HomeSettingsActivity) <= 512);
#endif
class ShellActivity final : public Activity {
  Library& library;
  Home home;
  UpgradeNotice notice;
  bool editorOnly = false, editorStarted = false;
#if CROSSINK_APP_CAP_HOME_EDITOR
  EditorAddRemove editor;
#if CROSSINK_APP_CAP_TOUCH
  // Fixed SDK hit table reused across frames; no naming-time heap allocation.
  static_assert(sizeof(freeink::ui::InteractionBuffer<56>) <= 2048);
  static_assert(sizeof(freeink::ui::KeyboardEntry) <= 32);
  mutable freeink::ui::InteractionBuffer<56> pageKeys;
  freeink::ui::KeyboardEntry pageEntry;
#endif
  bool namingAttached = false;
  mutable char defaultPageName[24]{};
#endif
  std::string bookPath;
  // One fallible <=6 KiB + TimeWidgetBatch allocation per Home lifetime: the immutable visible
  // page is too large for the task stack and must be freed before reader entry.
  std::unique_ptr<WidgetHost> widgets;
  uint8_t focus = 0, focusRow = 0;
  bool lastAvailable = false;
  bool compactHome() const {
    return widgets && widgets->page().revision ? !widgets->page().textOnly
                                               : std::string_view(Layout::deviceClass()) != "x3";
  }
  Rect homeSafeArea() const {
    // The approved footer is reserved once by tileRect, not again by native
    // button-hint metrics. Bezel-safe bounds still come from the native theme.
    return UITheme::getInstance().getScreenSafeArea(renderer, !compactHome(), false);
  }
  Rect homeHeader(Rect safe) const {
    return UITheme::getHeaderRect(renderer, compactHome() ? 64 : UITheme::getInstance().getMetrics().headerHeight,
                                  safe);
  }
  Rect tileRect(uint8_t index) const {
    const auto safe = homeSafeArea();
    const auto header = homeHeader(safe);
    const auto& pg =
#if CROSSINK_APP_CAP_HOME_EDITOR
        editor.draftPage() ? *editor.draftPage() :
#endif
                           widgets->page();
    const auto& p = pg.widgets[index];
    if (!pg.textOnly) {
      // Approved compact six-row viewport: 12 px side margins, 8 px gutters,
      // six px below the existing header and 42 px for the footer. Persisted
      // custom grids keep their own row/column counts and are never rewritten.
      const int top = header.y + header.height + 6;
      const int height = safe.y + safe.height - top - 42 + 8;
      const int width = safe.width - 24 + 8;
      return Rect{safe.x + 12 + width * p.x / pg.cols, top + height * p.y / pg.rows, width * p.w / pg.cols - 8,
                  height * p.h / pg.rows - 8};
    }
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
  const char* kindTitle(WidgetKind kind) const {
    switch (kind) {
      case WidgetKind::Clock:
        return tr(STR_HOME_CLOCK);
      case WidgetKind::Moon:
        return tr(STR_HOME_MOON);
      case WidgetKind::Glyph:
        return tr(STR_HOME_GLYPH);
      case WidgetKind::Continue:
        return tr(STR_CONTINUE_READING);
      case WidgetKind::Shelf:
        return tr(STR_HOME_SHELF);
      default:
        return tr(STR_HOME_BROWSE);
    }
  }
  const char* moonPhase(uint8_t phase) const {
    static constexpr StrId names[] = {StrId::STR_HOME_MOON_NEW,           StrId::STR_HOME_MOON_WAXING_CRESCENT,
                                      StrId::STR_HOME_MOON_FIRST_QUARTER, StrId::STR_HOME_MOON_WAXING_GIBBOUS,
                                      StrId::STR_HOME_MOON_FULL,          StrId::STR_HOME_MOON_WANING_GIBBOUS,
                                      StrId::STR_HOME_MOON_LAST_QUARTER,  StrId::STR_HOME_MOON_WANING_CRESCENT};
    return I18N.get(names[phase < 8 ? phase : 0]);
  }
  static bool glyphFace(uint32_t cp, void* context) {
    auto& self = *static_cast<ShellActivity*>(context);
    if (cp < 65 || cp > 90) return false;
    const auto& fonts = self.renderer.getFontMap();
    const auto serif = fonts.find(BITTER_16_FONT_ID);
    if (serif == fonts.end()) return false;
    const auto* actual = serif->second.getData(EpdFontFamily::REGULAR);
    const auto recovery = fonts.find(UI_12_FONT_ID);
    // S3's recovery path may bind the UI family under a reader ID. It must not
    // be labeled as the built-in serif sample. Probe the exact glyph, too.
    if (recovery != fonts.end() && actual == recovery->second.getData(EpdFontFamily::REGULAR)) return false;
    const auto found = serif->second.findGlyphData(cp, EpdFontFamily::REGULAR);
    return found.glyph && found.fontData == actual && self.renderer.getGlyphBitmap(found.fontData, found.glyph);
  }
  void paintTimeWidget(uint8_t index, Rect rect, bool selected) const {
    const auto kind = widgets->page().widgets[index].kind;
    const auto view = widgets->view(index);
    const auto* clock = widgets->clockCard(index);
    const auto* moon = widgets->moonCard(index);
    const auto* glyph = widgets->glyphCard(index);
    // Geometry scratch is bounded below 256 bytes; captions are reused per line
    // on the dedicated render stack. No per-paint allocation or source work.
    widgets::TimeDrawing drawing;
    const bool laidOut = clock ? widgets::Clock::layoutTime(*clock, widgetBox(index), drawing)
                         : moon ? widgets::Moon::layoutTime(*moon, widgetBox(index), drawing)
                         : glyph ? widgets::Glyph::layoutTime(*glyph, widgetBox(index), drawing) : false;
    renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, 1, 8, true);
    if (selected) renderer.drawRect(rect.x + 3, rect.y + 3, rect.width - 6, rect.height - 6, 2, true);
    if (!laidOut) {
      paintFittedText(UI_10_FONT_ID, rect.x + 10, rect.y + 6, kindTitle(kind), rect.width - 20);
      paintFittedText(SMALL_FONT_ID, rect.x + 10, rect.y + 32, clock || moon ? tr(STR_HOME_WIDGET_SMALL_TILE) : stateText(view.viewState), rect.width - 20);
      return;
    }
    // Small header symbols use fixed native primitives; no asset/source work.
    const int iconX = rect.x + rect.width - 26, iconY = rect.y + 7;
    static constexpr int8_t clockOutline[][2] = {{8, 0},  {12, 2}, {15, 6}, {15, 9}, {12, 13}, {8, 15},
                                                 {3, 13}, {0, 9},  {0, 6},  {3, 2},  {8, 0}};
    static constexpr int8_t moonOutline[][2] = {{12, 0}, {8, 3},  {7, 7}, {9, 11}, {13, 13}, {9, 15},
                                                {4, 14}, {1, 10}, {0, 6}, {3, 2},  {7, 0},   {12, 0}};
    const auto* outline = clock ? clockOutline : moonOutline;
    const unsigned outlineCount = glyph ? 0 : clock ? 11 : 12;
    for (unsigned n = 1; n < outlineCount; ++n)
      renderer.drawLine(iconX + outline[n - 1][0], iconY + outline[n - 1][1], iconX + outline[n][0],
                        iconY + outline[n][1]);
    if (glyph) {
      renderer.drawLine(iconX,iconY+15,iconX+7,iconY);
      renderer.drawLine(iconX+7,iconY,iconX+15,iconY+15);
      renderer.drawLine(iconX+3,iconY+10,iconX+12,iconY+10);
    }
    if (clock) {
      renderer.drawLine(iconX + 8, iconY + 3, iconX + 8, iconY + 8);
      renderer.drawLine(iconX + 8, iconY + 8, iconX + 12, iconY + 10);
    }
    if (moon && drawing.moon.width) {
      const auto& c = drawing.moon;
      const int radius = c.width / 2 - 1;
      for (int dy = -radius; dy <= radius; ++dy) {
        int half = radius;
        while (half * half + dy * dy > radius * radius) --half;
        const int cx = rect.x + c.x + c.width / 2, y = rect.y + c.y + c.height / 2 + dy;
        renderer.drawPixel(cx - half, y);
        renderer.drawPixel(cx + half, y);
        const int edge = half * moon->terminator / 1000;
        if (moon->fraction < 500)
          renderer.drawLine(cx + edge, y, cx + half, y);
        else
          renderer.drawLine(cx - half, y, cx - edge, y);
      }
    }
    if (glyph && drawing.glyph.width) {
      const auto& cell = drawing.glyph;
      const char sample[] = {static_cast<char>(glyph->codepoint), 0};
      const auto ink = renderer.getTextVerticalBounds(BITTER_16_FONT_ID, sample);
      const int height = ink.bottom - ink.top;
      if (height > 0) {
        int32_t advance = 0; int top = 0;
        if (renderer.getCodepointMetrics(BITTER_16_FONT_ID, glyph->codepoint, EpdFontFamily::REGULAR, advance, top)) {
          const int scale = std::min(44 * 256 / height, advance > 0 ? (cell.width - 4) * 16 * 256 / advance : 256);
          const int width = static_cast<int>(int64_t(advance) * scale / (16 * 256));
          const int baseline = rect.y + cell.y + (cell.height - height * scale / 256) / 2 +
            (renderer.getFontAscenderSize(BITTER_16_FONT_ID) - ink.top) * scale / 256;
          renderer.beginTextClip(rect.x + cell.x, rect.y + cell.y, cell.width, cell.height);
          renderer.drawScaledCodepoint(BITTER_16_FONT_ID, glyph->codepoint, EpdFontFamily::REGULAR,
            rect.x + cell.x + std::max(0,(cell.width-width)/2), baseline, scale);
          renderer.endTextClip();
        }
      }
    }
    for (uint8_t i = 0; i < drawing.count; ++i) {
      const auto& mark = drawing.marks[i];
      const auto& c = mark.cell;
      char caption[96]{};
      const char* text = caption;
      int font = SMALL_FONT_ID;
      switch (mark.line) {
        case widgets::TimeLine::Title:
          text = kindTitle(kind);
          font = UI_10_FONT_ID;
          break;
        case widgets::TimeLine::Value:
          font = BITTER_16_FONT_ID;
          if (clock->twelveHour) {
            const unsigned hour = clock->reading.local.hour;
            std::snprintf(caption, sizeof(caption), tr(STR_HOME_CLOCK_12H_FORMAT), hour % 12 ? hour % 12 : 12,
                          unsigned(clock->reading.local.minute), (hour < 12 ? tr(STR_AM) : tr(STR_PM)));
          } else {
            std::snprintf(caption, sizeof(caption), tr(STR_HOME_CLOCK_TIME_FORMAT), unsigned(clock->reading.local.hour),
                          unsigned(clock->reading.local.minute));
          }
          break;
        case widgets::TimeLine::Date: {
          static constexpr StrId weekdays[] = {StrId::STR_HOME_SUN_SHORT, StrId::STR_HOME_MON_SHORT,
                                               StrId::STR_HOME_TUE_SHORT, StrId::STR_HOME_WED_SHORT,
                                               StrId::STR_HOME_THU_SHORT, StrId::STR_HOME_FRI_SHORT,
                                               StrId::STR_HOME_SAT_SHORT};
          static constexpr StrId months[] = {
              StrId::STR_MONTH_JAN_SHORT, StrId::STR_MONTH_FEB_SHORT, StrId::STR_MONTH_MAR_SHORT,
              StrId::STR_MONTH_APR_SHORT, StrId::STR_MONTH_MAY_SHORT, StrId::STR_MONTH_JUN_SHORT,
              StrId::STR_MONTH_JUL_SHORT, StrId::STR_MONTH_AUG_SHORT, StrId::STR_MONTH_SEP_SHORT,
              StrId::STR_MONTH_OCT_SHORT, StrId::STR_MONTH_NOV_SHORT, StrId::STR_MONTH_DEC_SHORT};
          const auto& date = clock->reading.local;
          std::snprintf(caption, sizeof(caption), tr(STR_HOME_CLOCK_DATE_FORMAT),
                        I18N.get(weekdays[date.weekday < 7 ? date.weekday : 0]), unsigned(date.day),
                        I18N.get(months[date.month >= 1 && date.month <= 12 ? date.month - 1 : 0]),
                        unsigned(date.year));
          break;
        }
        case widgets::TimeLine::GlyphCode:
          std::snprintf(caption,sizeof(caption),tr(STR_HOME_GLYPH_CODE_FORMAT),static_cast<unsigned long>(glyph->codepoint)); break;
        case widgets::TimeLine::GlyphName:
          std::snprintf(caption,sizeof(caption),tr(STR_HOME_GLYPH_NAME_FORMAT),static_cast<char>(glyph->codepoint)); break;
        case widgets::TimeLine::GlyphSource: text = tr(STR_HOME_GLYPH_SERIF); break;
        case widgets::TimeLine::TimeSource:
          text = tr(STR_HOME_CLOCK_ESTABLISHED);
          break;
        case widgets::TimeLine::TimePhase:
          text = moonPhase(moon->phase);
          break;
        case widgets::TimeLine::Illumination:
          std::snprintf(caption, sizeof(caption), tr(STR_HOME_MOON_LIT_FORMAT), unsigned(moon->illumination));
          break;
        case widgets::TimeLine::Next:
          std::snprintf(caption, sizeof(caption), tr(STR_HOME_MOON_NEXT_FORMAT), moonPhase(moon->nextPhase),
                        unsigned((moon->nextSeconds + 43200) / 86400));
          break;
        case widgets::TimeLine::Stale:
          std::snprintf(caption, sizeof(caption), tr(STR_HOME_SAVED_DAYS_FORMAT),
                        static_cast<unsigned long>(clock ? clock->status.ageDays : moon ? moon->status.ageDays : glyph->status.ageDays));
          break;
        case widgets::TimeLine::Retry:
          text = tr(STR_RETRY);
          break;
        default:
          text = view.viewState == WidgetState::Off        ? tr(STR_HOME_SOURCE_OFF)
                 : view.viewState == WidgetState::NotSaved ? tr(STR_HOME_NOT_SAVED)
                 : view.viewState == WidgetState::Unsupported
                     ? (kind == WidgetKind::Clock ? tr(STR_HOME_CLOCK_UNSUPPORTED)
                     : kind == WidgetKind::Moon ? tr(STR_HOME_MOON_UNSUPPORTED)
                     : glyph && glyph->issue == GlyphIssue::FaceUnavailable ? tr(STR_HOME_GLYPH_FACE_UNAVAILABLE)
                     : tr(STR_HOME_MOON_UNSUPPORTED))
                 : view.viewState == WidgetState::Empty || view.viewState == WidgetState::Known
                     ? (kind == WidgetKind::Clock ? tr(STR_HOME_CLOCK_EMPTY) : kind == WidgetKind::Moon ? tr(STR_HOME_MOON_EMPTY) : tr(STR_HOME_GLYPH_EMPTY))
                     : stateText(view.viewState);
          break;
      }
      if (mark.line == widgets::TimeLine::State &&
          (view.viewState == WidgetState::Error || view.viewState == WidgetState::Oversize)) {
        std::snprintf(caption, sizeof(caption), "%s", tr(STR_HOME_WIDGET_DATA_ERROR));
        size_t split = 0, lastSpace = 0;
        while (caption[split]) {
          if (caption[split] == ' ') lastSpace = split;
          if ((static_cast<unsigned char>(caption[split + 1]) & 0xc0) == 0x80) {
            ++split;
            continue;
          }
          const char held = caption[split + 1];
          caption[split + 1] = 0;
          const bool fits = renderer.getTextWidth(SMALL_FONT_ID, caption) <= c.width;
          caption[split + 1] = held;
          if (!fits) break;
          ++split;
        }
        if (caption[split] && lastSpace) split = lastSpace;
        const bool wrapped = caption[split] != 0 && lastSpace != 0;
        if (wrapped) caption[split] = 0;
        renderer.beginTextClip(rect.x + c.x, rect.y + c.y, c.width, c.height);
        auto ink = renderer.getTextVerticalBounds(SMALL_FONT_ID, caption);
        paintFittedText(SMALL_FONT_ID, rect.x + c.x, rect.y + c.y - ink.top, caption, c.width);
        if (wrapped) {
          const auto* second = caption + split + 1;
          ink = renderer.getTextVerticalBounds(SMALL_FONT_ID, second);
          paintFittedText(SMALL_FONT_ID, rect.x + c.x, rect.y + c.y + 20 - ink.top, second, c.width);
        }
        renderer.endTextClip();
        continue;
      }
      renderer.beginTextClip(rect.x + c.x, rect.y + c.y, c.width, c.height);
      const auto style = mark.line == widgets::TimeLine::Title ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
      const auto ink = renderer.getTextVerticalBounds(font, text, style);
      const int inkHeight = ink.bottom - ink.top;
      const int textY = rect.y + c.y - ink.top + std::max(0, (c.height - inkHeight) / 2);
      paintFittedText(font, rect.x + c.x, textY, text, c.width, true, style);
      renderer.endTextClip();
    }
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
  void paintFittedText(int font, int x, int y, const char* text, int width, bool black = true,
                       EpdFontFamily::Style style = EpdFontFamily::REGULAR) const {
    char caption[96]{};
    size_t bytes = 0;
    while (bytes + 1 < sizeof(caption) && text[bytes]) {
      caption[bytes] = text[bytes];
      caption[++bytes] = 0;
      if ((static_cast<unsigned char>(text[bytes]) & 0xc0) == 0x80) continue;
      if (renderer.getTextWidth(font, caption, style) > width) {
        do {
          --bytes;
        } while (bytes && (static_cast<unsigned char>(caption[bytes]) & 0xc0) == 0x80);
        caption[bytes] = 0;
        break;
      }
    }
    renderer.drawText(font, x, y, caption, black, style);
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
    if (isTimeWidget(p.kind)) {
      paintTimeWidget(index, r, selected);
      return;
    }
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
  // Bounded UTF-8 wrapping shared by the static notice and text editor.
  int paragraph(const char* text, int x, int y, int width, int maxLines, bool bold = false,
                int font = UI_10_FONT_ID) const {
    char line[128]{};
    unsigned offset = 0;
    const int height = renderer.getLineHeight(font);
    for (int row = 0; row < maxLines && text[offset]; ++row) {
      unsigned n = 0, word = 0;
      while (text[offset + n] && n + 1 < sizeof(line)) {
        line[n] = text[offset + n];
        line[++n] = 0;
        if (line[n - 1] == ' ') word = n;
        if ((static_cast<unsigned char>(text[offset + n]) & 0xc0) == 0x80) continue;
        if (renderer.getTextWidth(font, line) > width) {
          if (word)
            n = word;
          else
            do {
              --n;
            } while (n && (static_cast<unsigned char>(line[n]) & 0xc0) == 0x80);
          break;
        }
      }
      if (!n) break;
      line[n] = 0;
      renderer.drawText(font, x, y, line, true, bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
      y += height;
      offset += n;
      while (text[offset] == ' ') ++offset;
    }
    return y;
  }
  Rect noticeAction(uint8_t row) const {
    const auto safe = homeSafeArea();
    const int height = renderer.getLineHeight(UI_10_FONT_ID) + 22;
    return Rect{safe.x + 24, safe.y + safe.height - 42 - (2 - row) * (height + 8), safe.width - 48, height};
  }
  void paintNotice() const {
    const auto safe = homeSafeArea();
    const int x = safe.x + 24, width = safe.width - 48;
    int y = safe.y + 12;
    paintFittedText(UI_10_FONT_ID, x, y, tr(STR_BACK), 68);
    y = paragraph(tr(STR_BOOKSYNC_NAME), x + 88, y, width - 88, 1, true);
    y = paragraph(tr(STR_UPGRADE_FROM), x + 88, y, width - 88, 1) + 18;
    y = paragraph(tr(STR_UPGRADE_TITLE), x, y, width, 2, true, BITTER_14_FONT_ID) + 18;
    y = paragraph(tr(STR_UPGRADE_CARRIED), x, y, width, 1, true) + 4;
    y = paragraph(tr(STR_UPGRADE_RETAINED), x, y, width, 5) + 16;
    y = paragraph(tr(STR_UPGRADE_FRESH), x, y, width, 1, true) + 4;
    y = paragraph(tr(STR_UPGRADE_DEFAULTS), x, y, width, 2) + 16;
    y = paragraph(tr(STR_UPGRADE_RESTORE), x, y, width, 1, true) + 4;
    char text[192]{};
    std::snprintf(text, sizeof(text), tr(STR_UPGRADE_RESTORE_FORMAT), notice.serviceName());
    paragraph(text, x, y, width, 4);
    for (uint8_t i = 0; i < 2; ++i) {
      const auto r = noticeAction(i);
      const bool selected = notice.focus() == i;
      if (selected)
        renderer.fillRect(r.x, r.y, r.width, r.height, true);
      else
        renderer.drawRect(r.x, r.y, r.width, r.height, 1, true);
      if (!i) std::snprintf(text, sizeof(text), tr(STR_UPGRADE_SYNC_FORMAT), notice.serviceName());
      paintFittedText(UI_10_FONT_ID, r.x + 12, r.y + 8, i ? tr(STR_LATER) : text, r.width - 24, !selected);
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_HOME_PREV), tr(STR_HOME_NEXT));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  bool noticeInput() {
    if (!notice.visible()) return false;
    int x = 0, y = 0;
    if (mappedInput.wasReleased(MappedInputManager::Button::Back))
      notice.back();
    else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm))
      notice.choose(notice.focus());
    else if (mappedInput.wasReleased(mappedInput.menuButton(MappedInputManager::Button::Down)))
      notice.stepNotice(1);
    else if (mappedInput.wasReleased(mappedInput.menuButton(MappedInputManager::Button::Up)))
      notice.stepNotice(-1);
    else if (mappedInput.wasScreenTapped(x, y)) {
      for (uint8_t i = 0; i < 2; ++i) {
        const auto r = noticeAction(i);
        if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height) notice.choose(i);
      }
      const auto safe = homeSafeArea();
      if (y >= safe.y && y < safe.y + 2 * renderer.getLineHeight(UI_10_FONT_ID) && x >= safe.x && x < safe.x + 100)
        notice.back();
    } else
      return true;
    requestUpdate();
    return true;
  }
#if CROSSINK_APP_CAP_HOME_EDITOR
  Rect textEditorRow(uint8_t row) const {
    const auto safe = homeSafeArea();
    const auto header = homeHeader(safe);
    const int line = renderer.getLineHeight(UI_10_FONT_ID);
    const int top = header.y + header.height + (editor.panel() == EditorAddRemove::Panel::Select ? 12 : 6 * line);
    const int height = line + 18;
    const int capacity = std::max(1, (safe.y + safe.height - 48 - top) / height);
    const int first = editor.focus() >= capacity ? editor.focus() - capacity + 1 : 0;
    return Rect{safe.x + 24, top + (row - first) * height, safe.width - 48, height};
  }
  void paintTextEditor() const {
    using Panel = EditorAddRemove::Panel;
    const auto panel = editor.panel();
    const auto safe = homeSafeArea();
    const auto header = homeHeader(safe);
    const char* title = nullptr;
    const char* body = nullptr;
    char selectedTitle[96]{};
    if (panel == Panel::Selected) {
      const auto kind = widgets->page().widgets[editor.selected()].kind;
      std::snprintf(selectedTitle, sizeof(selectedTitle), tr(STR_HOME_REMOVE_CONFIRM_FORMAT),
                    kind == WidgetKind::Continue ? tr(STR_CONTINUE_READING)
                    : kind == WidgetKind::Shelf  ? tr(STR_HOME_SHELF)
                                                 : tr(STR_HOME_BROWSE));
      title = selectedTitle;
      body = tr(STR_HOME_REMOVE_EXPLAIN);
    } else if (panel == Panel::Add) {
      title = tr(STR_HOME_ADD_WIDGET);
      body = tr(STR_HOME_ADD_EXPLAIN);
    } else if (panel == Panel::Reset) {
      title = tr(STR_HOME_RESET_TITLE);
      body = tr(STR_HOME_RESET_EXPLAIN);
    } else if (panel == Panel::Error) {
      const bool noSpace = editor.editorFailure() == LayoutEditStatus::NoSpace;
      title = noSpace ? tr(STR_HOME_ADD_FAILED) : tr(STR_HOME_LAYOUT_NOT_SAVED);
      body = noSpace ? tr(STR_HOME_NO_SPACE_EXPLAIN) : tr(STR_HOME_NOT_SAVED_EXPLAIN);
    } else if (panel == Panel::Waiting)
      title = tr(STR_HOME_EDIT_SAVING);
    int y = header.y + header.height + 12;
    if (title) y = paragraph(title, safe.x + 24, y, safe.width - 48, 2, true);
    if (body) paragraph(body, safe.x + 24, y + 8, safe.width - 48, 3);
    if (panel == Panel::Waiting) return;
    const int minY = panel == Panel::Select ? header.y + header.height + 12
                                            : header.y + header.height + 6 * renderer.getLineHeight(UI_10_FONT_ID);
    for (uint8_t i = 0; i < editor.rows(); ++i) {
      const auto r = textEditorRow(i);
      if (r.y < minY || r.y + r.height > safe.y + safe.height - 48) continue;
      const bool selected = i == editor.focus();
      if (selected)
        renderer.fillRect(r.x, r.y, r.width, r.height, true);
      else
        renderer.drawRect(r.x, r.y, r.width, r.height, 1, true);
      char label[96]{};
      const char* text = editorLabel(i);
      if (panel == Panel::Select && i < widgets->page().count && widgets->page().widgets[i].kind == WidgetKind::Shelf) {
        std::snprintf(label, sizeof(label), tr(STR_HOME_SHELF_SOURCE_FORMAT),
                      widgets->page().widgets[i].source == WindowSource::Recent
                          ? tr(STR_HOME_EDITOR_RECENT)
                          : shelfTitle(widgets->page().widgets[i].source));
        text = label;
      }
      paintFittedText(UI_10_FONT_ID, r.x + 12, r.y + 8, text, r.width - 24, !selected);
    }
  }

#endif
  HalDisplay::RefreshMode initialRefresh;

#if CROSSINK_APP_CAP_HOME_EDITOR
  bool canEdit() const { return widgets && widgets->page().revision; }
  const char* editorLabel(uint8_t row) const {
    using Panel = EditorAddRemove::Panel;
    const auto panel = editor.panel();
    if (panel == Panel::Selected || panel == Panel::Options) {
      if (widgets->page().textOnly) return row == 0 ? tr(STR_HOME_KEEP_WIDGET) : tr(STR_HOME_REMOVE_WIDGET);
      return row == 0 ? tr(STR_HOME_REMOVE_WIDGET) : tr(STR_DONE);
    }
    if (panel == Panel::Name) {
      if (row == 0) return editor.pageName();
      if (!mappedInput.hasTouch() && row <= 10) return pageNameOption(row - 1);
      return row == (mappedInput.hasTouch() ? 1 : 11) ? tr(STR_CANCEL) : tr(STR_SAVE);
    }
    if (panel == Panel::Reset) return row == 0 ? tr(STR_HOME_KEEP_LAYOUT) : tr(STR_HOME_RESET_LAYOUT);
    if (panel == Panel::Error) return tr(STR_BACK);
    if (panel == Panel::Add) return kindTitle(static_cast<WidgetKind>(row));
    const auto count = widgets->page().count;
    if (row < count) {
      const auto kind = widgets->page().widgets[row].kind;
      return kindTitle(kind);
    }
    if (row >= count + 3) {
      const char* labels[] = {tr(STR_HOME_PAGE_PREVIOUS),    tr(STR_HOME_PAGE_NEXT), tr(STR_HOME_PAGE_ADD),
                              tr(STR_HOME_PAGE_NAME_ACTION), tr(STR_HOME_PAGE_LEFT), tr(STR_HOME_PAGE_RIGHT)};
      return labels[row - count - 3];
    }
    return row == count ? tr(STR_HOME_ADD_WIDGET) : row == count + 1 ? tr(STR_HOME_RESET_LAYOUT) : tr(STR_DONE);
  }
  Rect editorHeaderButton(uint8_t index) const {
    const auto safe = homeSafeArea();
    const auto header = homeHeader(safe);
    const int width = (header.width - 24) / (editor.panel() == EditorAddRemove::Panel::Selected ? 2 : 3);
    const int height = renderer.getLineHeight(UI_10_FONT_ID) + 12;
    return Rect{header.x + 12 + index * width, header.y + renderer.getLineHeight(BITTER_14_FONT_ID) + 8, width - 4,
                height < 50 ? 50 : height};
  }
  Rect editorRow(uint8_t index) const {
    const auto safe = homeSafeArea();
    const auto header = homeHeader(safe);
    const int line = renderer.getLineHeight(UI_10_FONT_ID);
    const int height = line + 26;
    if (editor.panel() == EditorAddRemove::Panel::Name && mappedInput.hasTouch()) {
      const int keyboardHeight = safe.height * 45 / 100 < 250 ? safe.height * 45 / 100 : 250;
      const int top = header.y + header.height + line + 24;
      const int available = safe.y + safe.height - keyboardHeight - top - 16;
      const int rowHeight = available / 2 < 50 ? available / 2 : 50;
      if (index == 0) return Rect{safe.x + 24, top, safe.width - 48, rowHeight};
      const int width = (safe.width - 56) / 2;
      return Rect{safe.x + 24 + (index - 1) * (width + 8), top + rowHeight + 8, width, rowHeight};
    }
    if (editor.panel() == EditorAddRemove::Panel::Name) {
      const int top = header.y + header.height + line + 24;
      const int available = safe.y + safe.height - top - 16;
      const int rowHeight = available / 9 < 46 ? available / 9 : 46;
      if (index == 0) return Rect{safe.x + 24, top, safe.width - 48, rowHeight};
      if (index <= 10) {
        const int start = editor.focus() >= 8 ? 4 : 1;
        return Rect{safe.x + 24, top + (int(index) - start + 1) * rowHeight, safe.width - 48, rowHeight};
      }
      const int width = (safe.width - 56) / 2;
      return Rect{safe.x + 24 + (index - 11) * (width + 8), top + 8 * rowHeight, width, rowHeight};
    }
    return Rect{safe.x + 24, header.y + header.height + line * 3 + 28 + index * height, safe.width - 48, height};
  }
  Rect editorPageButton(uint8_t index) const {
    const auto safe = homeSafeArea();
    const int width = (safe.width - 16) / 6;
    // Compact button labels fit above Classic's guides without shrinking tiles.
    const int height = mappedInput.hasTouch() ? 52 : renderer.getLineHeight(SMALL_FONT_ID) + 8;
    return Rect{safe.x + 8 + index * width, safe.y + safe.height - 54, width - 2, height};
  }
  bool pageActionEnabled(uint8_t index) const {
    const auto& page = widgets->page();
    if (index == 0 || index == 4) return page.page > 0;
    if (index == 1 || index == 5) return page.page + 1 < page.pages;
    return index != 2 || page.pages < 16;
  }
  void syncPageEntry() {
    const bool naming = editor.panel() == EditorAddRemove::Panel::Name;
    if (naming && !namingAttached) {
      if (!editor.pageName()[0]) editor.namePage(tr(STR_HOME));
#if CROSSINK_APP_CAP_TOUCH
      pageEntry.attach(editor.nameBuffer(), 81);
#endif
    }
    namingAttached = naming;
  }
  const char* pageNameOption(uint8_t index) const {
    const char* names[] = {tr(STR_HOME),
                           tr(STR_HOME_PAGE_READING),
                           tr(STR_LIBRARY),
                           tr(STR_HOME_PAGE_LISTS),
                           tr(STR_HOME_WANT_TO_READ),
                           tr(STR_HOME_PAGE_STATS),
                           tr(STR_HOME_PAGE_SHELVES),
                           tr(STR_HOME_SERIES),
                           tr(STR_HOME_PAGE_FAVOURITES)};
    if (index < 9) return names[index];
    std::snprintf(defaultPageName, sizeof(defaultPageName), tr(STR_HOME_PAGE_DEFAULT_FORMAT),
                  unsigned(widgets->page().page + 1));
    return defaultPageName;
  }
  void cyclePageName() {
    uint8_t current = 0;
    while (current < 10 && std::strcmp(editor.pageName(), pageNameOption(current))) ++current;
    editor.namePage(pageNameOption(current < 9 ? current + 1 : 0));
  }
  const char* firstWidgetPageName(uint8_t widget) const {
    if (widget >= unsigned(WidgetKind::Clock)) return kindTitle(static_cast<WidgetKind>(widget));
    return widget == 0 ? tr(STR_HOME_PAGE_READING) : widget == 1 ? tr(STR_HOME_RECENT) : tr(STR_LIBRARY);
  }
  void paintPageKeyboard() const {
#if CROSSINK_APP_CAP_TOUCH
    if (!mappedInput.hasTouch()) return;
    namespace fui = freeink::ui;
    const auto safe = homeSafeArea();
    fui::GfxRendererTarget target(renderer);
    target.setFont(fui::GfxRendererTarget::FONT_SMALL, UI_10_FONT_ID);
    target.setFont(fui::GfxRendererTarget::FONT_BODY, UI_12_FONT_ID);
    const auto device = target.deviceContext();
    const fui::InputSnapshot noInput{};
    pageKeys.beginPublishCycle();
    fui::Frame<56> frame(target, device, noInput, pageKeys);
    fui::QwertyKeyboardProps props;
    fui::applyEntry(props, pageEntry);
    props.keyAction = 1;
    props.shiftAction = 2;
    props.modeAction = 3;
    props.deleteAction = 4;
    props.okAction = 5;
    props.okLabel = tr(STR_SAVE);
    props.modeLabel = pageEntry.symbols ? tr(STR_KEY_MODE_ABC) : tr(STR_KEY_MODE_SYMBOLS);
    // Keyboard is docked at the usable bottom; field and actions stay above it.
    const int16_t height = safe.height * 45 / 100 < 250 ? safe.height * 45 / 100 : 250;
    fui::qwertyKeyboard(frame,
                        {static_cast<int16_t>(safe.x), static_cast<int16_t>(safe.y + safe.height - height),
                         static_cast<int16_t>(safe.width), height},
                        props);
    pageKeys.publish();
#endif
  }
  Rect editorMoveButton(uint8_t index) const {
    const auto safe = homeSafeArea();
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
      const auto safe = homeSafeArea();
      renderer.fillRect(safe.x + 8, safe.y + safe.height - 54, safe.width - 16, 52, true);
      renderer.drawText(UI_10_FONT_ID, safe.x + 16, safe.y + safe.height - 52, tr(STR_HOME_EDIT_CARRY_MOVE), false);
      renderer.drawText(UI_10_FONT_ID, safe.x + 16, safe.y + safe.height - 28, tr(STR_HOME_EDIT_CARRY_DROP), false);
    }
  }
  void paintEditor() const {
    using Panel = EditorAddRemove::Panel;
    const auto panel = editor.panel();
    if (!widgets->page().textOnly && (panel == Panel::Select || panel == Panel::Selected)) {
      // Reuse the Home tiles and snapped grid; no second framebuffer.
      for (uint8_t i = 0; i < widgets->page().count; ++i) paintWidget(i);
      for (uint8_t i = 0; i < (panel == Panel::Selected ? 2 : 3); ++i) {
        const auto r = editorHeaderButton(i);
        const auto row = panel == Panel::Selected ? i : widgets->page().count + i;
        renderer.drawRect(r.x, r.y, r.width, r.height, panel == Panel::Select && editor.focus() == row ? 3 : 1, true);
        paintFittedText(UI_10_FONT_ID, r.x + 5, r.y + 4, editorLabel(row), r.width - 10);
      }
      if (panel == Panel::Select) {
        const auto safe = homeSafeArea();
        const auto header = homeHeader(safe);
        char number[24];
        std::snprintf(number, sizeof(number), tr(STR_HOME_PAGE_FORMAT), widgets->page().page + 1,
                      widgets->page().pages);
        paintFittedText(UI_10_FONT_ID, header.x + header.width - 72, header.y + 4, number, 68);
        for (uint8_t i = 0; i < 6; ++i) {
          const auto r = editorPageButton(i);
          const auto row = widgets->page().count + 3 + i;
          renderer.drawRect(r.x, r.y, r.width, r.height, editor.focus() == row ? 3 : 1, true);
          const int font = SMALL_FONT_ID;
          const int labelY = r.y + (r.height - renderer.getTextHeight(font)) / 2;
          paintFittedText(font, r.x + 4, labelY, editorLabel(row), r.width - 8);
          if (!pageActionEnabled(i))
            renderer.drawLine(r.x + 4, r.y + r.height - 3, r.x + r.width - 4, r.y + r.height - 3, 1, true);
        }
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
    if (widgets->page().textOnly) {
      paintTextEditor();
      return;
    }
    const auto safe = homeSafeArea();
    const auto header = homeHeader(safe);
    const char* title = panel == Panel::Name                                  ? tr(STR_HOME_PAGE_RENAME)
                        : panel == Panel::Paging                              ? tr(STR_HOME_EDIT_SAVING)
                        : panel == Panel::Add                                 ? tr(STR_HOME_ADD_WIDGET)
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
    if (panel == Panel::Waiting || panel == Panel::Paging) return;
    for (uint8_t i = 0; i < editor.rows(); ++i) {
      if (panel == Panel::Name && !mappedInput.hasTouch() && i >= 1 && i <= 10) {
        const uint8_t start = editor.focus() >= 8 ? 4 : 1;
        if (i < start || i >= start + 7) continue;
      }
      const auto r = editorRow(i);
      renderer.drawRect(r.x, r.y, r.width, r.height, i == editor.focus() ? 3 : 1, true);
      paintFittedText(UI_10_FONT_ID, r.x + 12, r.y + 10, editorLabel(i), r.width - 24);
    }
    if (panel == Panel::Name) {
      if (!mappedInput.hasTouch()) {
        const auto actions = editorRow(11);
        paintFittedText(UI_10_FONT_ID, safe.x + 24, actions.y + actions.height + 12, tr(STR_HOME_PAGE_NAME_NEXT),
                        safe.width - 48);
      }
      paintPageKeyboard();
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
      const auto safe = homeSafeArea();
      const auto header = homeHeader(safe);
      if (mappedInput.wasReleased(mappedInput.menuButton(MappedInputManager::Button::Right)) ||
          (tapped && x >= header.x + header.width - 80 && y >= header.y && y < header.y + header.height)) {
        if (widgets->page().textOnly) {
#if CROSSINK_APP_CAP_HOME_EDITOR_SETTINGS
          showHomeSettings(renderer, mappedInput);
#endif
        } else
          editor.enter();
        requestUpdate();
        return true;
      }
      return false;
    }
    const auto safe = homeSafeArea();
    const auto header = homeHeader(safe);
    const bool touchBack = tapped && editor.panel() != Panel::Select && editor.panel() != Panel::Selected &&
                           x >= header.x + header.width - 80 && x < header.x + header.width && y >= header.y &&
                           y < header.y + header.height;
#if CROSSINK_APP_CAP_TOUCH
    if (tapped && editor.panel() == Panel::Name && mappedInput.hasTouch()) {
      freeink::ui::InputSnapshot input{};
      input.touchReleased = true;
      input.touchX = x;
      input.touchY = y;
      const auto event = pageKeys.routePublished(input);
      if (event) {
        switch (event.action) {
          case 1:
            pageEntry.key(event.value);
            break;
          case 2:
            pageEntry.shift();
            break;
          case 3:
            pageEntry.mode();
            break;
          case 4:
            pageEntry.backspace();
            break;
          case 5:
            editor.chooseEditor(2);
            break;
        }
        syncPageEntry();
        requestUpdate();
        return true;
      }
    }
#endif
    if (touchBack || mappedInput.wasReleased(MappedInputManager::Button::Back))
      editor.back();
    else if (mappedInput.wasReleased(mappedInput.menuButton(MappedInputManager::Button::Right)))
      editor.moreEditor();
    else if (mappedInput.wasReleased(mappedInput.menuButton(MappedInputManager::Button::Down)))
      editor.stepEditor(1);
    else if (mappedInput.wasReleased(mappedInput.menuButton(MappedInputManager::Button::Up)))
      editor.stepEditor(-1);
    else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (editor.panel() == Panel::Name && editor.focus() == 0)
        cyclePageName();
      else if (editor.panel() == Panel::Name && !mappedInput.hasTouch() && editor.focus() <= 10)
        editor.namePage(pageNameOption(editor.focus() - 1));
      else {
        if (editor.panel() == Panel::Add) editor.namePage(firstWidgetPageName(editor.focus()));
        editor.activateEditor();
      }
    } else if (tapped) {
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
        if (editor.panel() == Panel::Select) {
          for (uint8_t i = 0; i < 6; ++i) {
            const auto r = editorPageButton(i);
            if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height && pageActionEnabled(i)) {
              editor.chooseEditor(widgets->page().count + 3 + i);
              syncPageEntry();
              requestUpdate();
              return true;
            }
          }
          if (x >= header.x && x < header.x + header.width - 80 && y >= header.y &&
              y < header.y + renderer.getLineHeight(BITTER_14_FONT_ID) + 8) {
            editor.chooseEditor(widgets->page().count + 6);
            syncPageEntry();
            requestUpdate();
            return true;
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
            if (editor.panel() == Panel::Add) editor.namePage(firstWidgetPageName(i));
            editor.chooseEditor(i);
            break;
          }
        }
    } else {
      return true;  // Consume the editor context without scheduling an idle repaint.
    }
    syncPageEntry();
    requestUpdate();
    return true;
  }
#else
  bool editorInput() { return false; }
#endif
 public:
  ShellActivity(GfxRenderer& renderer, MappedInputManager& input, HalDisplay::RefreshMode refresh,
                Library& library = offlineLibrary(), bool editorOnly = false)
      : Activity("Home", renderer, input),
        library(library),
        home(library),
        notice(library),
        editorOnly(editorOnly),
#if CROSSINK_APP_CAP_HOME_EDITOR
        editor(library, input.hasTouch()),
#endif
        initialRefresh(refresh) {
  }
  bool isHomeActivity() const override { return !editorOnly; }
  void onEnter() override {
    Activity::onEnter();
#if CROSSINK_SCALABLE_FONTS
    // Reuse the reader's fallible built-in serif loader on S3. It prepares the
    // existing font family once, outside paint, and supplies its UI fallback on
    // allocation failure. C3 keeps its flash-resident bitmap family.
    ensureScalableBuiltinFamily(renderer, 1);
#endif
    enterLocalLibrary();
    setNetworkReading(editorOnly || !installNetworkMemory());
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
    bool localWorked = false;
    // Keep the local index's SD work outside the admitted network worker phase.
    if (!networkAdmission().busy()) {
      RenderLock lock(RenderLock::Mode::Try);
      if (lock.ownsLock()) {
        workLocalLibrary();
        localWorked = true;
      }
    }
    {
      RenderLock lock;
      workTimeWidgets(SETTINGS.clockFormat == 1, glyphFace, this);
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
#if CROSSINK_APP_CAP_HOME_EDITOR
      if (editorOnly && !editorStarted && !canEdit() && localWorked) {
        LOG_ERR("BSH", "Home editor unavailable; returning to Settings");
        finish();
        return;
      }
      if (editorOnly && !editorStarted && canEdit()) {
        editor.enter();
        editorStarted = true;
        requestUpdate();
      }
      if (editorOnly && editorStarted && editor.panel() == EditorAddRemove::Panel::Closed) {
        // A failed render-lock try must not discard an accepted edit on exit.
        if (!localWorked) return;
        finish();
        return;
      }
#endif
      if (!editorOnly && notice.pollNotice(ReadingGate::mayShow(
                             Presentation::Message, {false, activityManager.isCurrentActivity(this), false})))
        requestUpdate();
      if (noticeInput()) return;
      if (editorInput()) return;
      // A valid empty page still owns navigation; Confirm cannot open a hidden book.
      if (widgets && widgets->page().revision) {
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
        if (count && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
          if (!widgets->retryWidget(focus) && focusRow < widgets->widgetRows(focus, widgetBox(focus)))
            widgets->dispatchWidget(focus, focusRow);
        }
        int x = 0, y = 0;
        if (mappedInput.wasScreenTapped(x, y)) {
          for (uint8_t i = 0; i < count; ++i) {
            const auto r = tileRect(i);
            if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height) {
              if (isTimeWidget(widgets->page().widgets[i].kind) &&
                  widgets->hitWidgetRow(i, widgetBox(i), x - r.x, y - r.y) == 255)
                continue;
              if (!widgets->retryWidget(i)) {
                const auto row = widgets->hitWidgetRow(i, widgetBox(i), x - r.x, y - r.y);
                if (row != 255) widgets->dispatchWidget(i, row);
              }
            }
          }
          const auto safe = homeSafeArea();
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
    // Keep Settings visible until its child has a complete editor projection.
#if CROSSINK_APP_CAP_HOME_EDITOR
    if (editorOnly && (!editorStarted || editor.panel() == EditorAddRemove::Panel::Closed)) return;
#endif
#ifdef SIMULATOR
    ++scenePaints;
#endif
    renderer.clearScreen();
    if (notice.visible()) {
      paintNotice();
      renderer.displayBuffer(initialRefresh);
      initialRefresh = HalDisplay::FAST_REFRESH;
      return;
    }
    const auto safe = homeSafeArea();
    const auto header = homeHeader(safe);
#if CROSSINK_APP_CAP_HOME_EDITOR
    const bool editing = editor.panel() != EditorAddRemove::Panel::Closed;
#else
    constexpr bool editing = false;
#endif
    const char* title = editing && widgets->page().textOnly ? tr(STR_HOME_WIDGETS)
                        : widgets && widgets->page().title[0] ? widgets->page().title.data()
                                                              : tr(STR_HOME);
#if CROSSINK_APP_CAP_HOME_EDITOR
    if (editing && !widgets->page().textOnly && editor.panel() != EditorAddRemove::Panel::Select)
      title = tr(STR_HOME_EDIT);
    if (editing && editor.draftPage()) {
      const auto kind = editor.draftPage()->widgets[editor.selected()].kind;
      title = kindTitle(kind);
    }
#endif
    int titleWidth = header.width - 24;
#if CROSSINK_APP_CAP_HOME_EDITOR
    if (canEdit()) titleWidth -= 80;
#endif
    renderer.beginTextClip(header.x + 12, header.y, titleWidth, header.height);
    renderer.drawText(BITTER_14_FONT_ID, header.x + 12, header.y + 4, title, true, EpdFontFamily::BOLD);
    renderer.drawText(compactHome() ? SMALL_FONT_ID : UI_10_FONT_ID, header.x + 12,
                      header.y + (compactHome() ? 42 : renderer.getLineHeight(BITTER_14_FONT_ID) + 6),
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
      paintFittedText(UI_10_FONT_ID, header.x + header.width - 72, header.y + 4,
                      widgets->page().textOnly ? tr(STR_SETTINGS_TITLE) : tr(STR_EDIT), 68);
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
  publishRetainedAccountView();
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
  const auto result =
      account.removeLogin(scope < 2 ? LoginKind::Sync : LoginKind::Acquisition, account.snapshot().ticket);
  if (scope == 0) publishRetainedAccountView();
  return result;
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
void showHomeSettings(GfxRenderer& renderer, MappedInputManager& input) {
#if CROSSINK_APP_CAP_HOME_EDITOR_SETTINGS
  // One fallible <=512-byte activity allocation: ActivityManager owns it across
  // frames, so it cannot be stack-local. No task, payload buffer or framebuffer.
  auto settings = makeUniqueNoThrow<HomeSettingsActivity>(renderer, input);
  if (!settings) {
    LOG_ERR("BSH", "Cannot allocate Home settings");
    return;
  }
  activityManager.replaceActivity(std::move(settings));
#else
  (void)renderer;
  (void)input;
#endif
}
std::unique_ptr<Activity> createHomeEditor(GfxRenderer& renderer, MappedInputManager& input) {
#if CROSSINK_APP_CAP_HOME_EDITOR
  auto shell = makeUniqueNoThrow<ShellActivity>(renderer, input, HalDisplay::FULL_REFRESH, offlineLibrary(), true);
  if (!shell) LOG_ERR("BSH", "Cannot allocate Home editor");
  return shell;
#else
  return nullptr;
#endif
}
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
void showHomeScene(GfxRenderer& renderer, MappedInputManager& input, Library& library, bool edit) {
  auto shell = makeUniqueNoThrow<ShellActivity>(renderer, input, HalDisplay::FULL_REFRESH, library, edit);
  if (!shell) {
    LOG_ERR("BSH", "Cannot allocate Home scene");
    return;
  }
  activityManager.replaceActivity(std::move(shell));
}
#endif
}  // namespace booksync
