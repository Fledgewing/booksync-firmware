#include "Bootstrap.h"

#include <FreeInkUIGfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <ScalableBuiltins.h>
#include <Utf8.h>

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
#include "DeviceRuntime.h"
#include "NetworkPhase.h"
#include "activities/network/WifiSelectionActivity.h"
#include "booksync/services/Layout.h"
#ifndef SIMULATOR
#include <WiFi.h>
#endif
#include "ReadingGate.h"
#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "booksync/services/LocalLibrary.h"
#include "booksync/services/OfflineLibrary.h"
#include "booksync/system/CatalogScreen.h"
#include "booksync/ui/EditorAddRemove.h"
#include "booksync/ui/Home.h"
#include "booksync/ui/WidgetHost.h"
#include "booksync/ui/widgets/Forecast.h"
#include "booksync/ui/widgets/Goal.h"
#include "booksync/ui/widgets/Monthly.h"
#include "booksync/ui/widgets/Pages.h"
#include "booksync/ui/widgets/Records.h"
#include "booksync/ui/widgets/Streak.h"
#include "booksync/ui/widgets/Today.h"
#include "booksync/ui/widgets/Week.h"
#include "booksync/ui/widgets/When.h"
#include "booksync/ui/widgets/Year.h"
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
  // One fallible <=6 KiB allocation per Home lifetime: the immutable visible
  // page is too large for the task stack and must be freed before reader entry.
  std::unique_ptr<WidgetHost> widgets;
  // Reused <=160-byte paint scratch avoids adding a chart array to each render frame.
  mutable widgets::StatsDrawing statsDrawing;
  uint8_t focus = 0, focusRow = 0;
  bool lastAvailable = false;
  static bool deviceNavigation(void* context, DeviceCommand command) {
    if (command.action != DeviceAction::Wifi) return false;
    auto& self = *static_cast<ShellActivity*>(context);
    if (command.value) {
      // Reuse the approved native foreground network picker and its keyboard.
      // Its controller outlives this frame and is owned by ActivityManager.
      auto picker = makeUniqueNoThrow<WifiSelectionActivity>(self.renderer, self.mappedInput, true);
      if (!picker) {
        LOG_ERR("BSD", "Cannot allocate native Wi-Fi picker");
        return false;
      }
      self.startActivityForResult(std::move(picker),
                                  [](const ActivityResult&) { activityManager.persistGlobalSettings(); });
      return true;
    }
#ifndef SIMULATOR
    // DeviceBridge and DeviceRuntime both require the sole network owner idle.
    // No transport/task is stopped underneath an admitted worker.
    WiFi.disconnect(false);
    if (!WiFi.mode(WIFI_OFF)) {
      LOG_ERR("BSD", "Native Wi-Fi stop deferred");
      return false;
    }
#endif
    return true;
  }
  Rect tileRect(uint8_t index) const {
    const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const auto header = UITheme::getHeaderRect(renderer, UITheme::getInstance().getMetrics().headerHeight, safe);
    const auto& pg =
#if CROSSINK_APP_CAP_HOME_EDITOR
        editor.draftPage() ? *editor.draftPage() :
#endif
                           widgets->page();
    const auto& p = pg.widgets[index];
    if (!pg.textOnly && pg.rows == 6) {
      // Approved compact tokens are minimum insets; larger calibrated bezel
      // bounds and native hints still constrain the grid.
      int top, right, bottom, left;
      renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
      const int gridLeft = std::max(left, 12);
      const int gridRight = renderer.getScreenWidth() - std::max(right, 12);
      const int gridTop = std::max(top, 64 + 6);
      const int gridBottom = std::min(safe.y + safe.height, renderer.getScreenHeight() - std::max(bottom, 42));
      const int stepX = (gridRight - gridLeft + 8) / pg.cols;
      const int stepY = (gridBottom - gridTop + 8) / pg.rows;
      return Rect{gridLeft + stepX * p.x, gridTop + stepY * p.y, stepX * p.w - 8, stepY * p.h - 8};
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
  const char* widgetTitle(WidgetKind kind) const {
    if (kind == WidgetKind::Goal) return tr(STR_HOME_STATS_GOAL);
    if (kind == WidgetKind::Forecast) return tr(STR_HOME_STATS_FORECAST);
    if (kind == WidgetKind::When) return tr(STR_HOME_STATS_WHEN);
    if (kind == WidgetKind::Records) return tr(STR_HOME_STATS_RECORDS);
    if (kind == WidgetKind::Today) return tr(STR_HOME_STATS_TODAY);
    if (kind == WidgetKind::Pages) return tr(STR_HOME_STATS_PAGES);
    if (kind == WidgetKind::Year) return tr(STR_HOME_YEAR_IN_READING);
    if (kind == WidgetKind::Monthly) return tr(STR_HOME_MONTHLY_READING);
    if (kind == WidgetKind::Week) return tr(STR_HOME_THIS_WEEK);
    if (kind == WidgetKind::Streak) return tr(STR_HOME_READING_STREAK);
    return kind == WidgetKind::Continue ? tr(STR_CONTINUE_READING)
           : kind == WidgetKind::Shelf  ? tr(STR_HOME_SHELF)
                                        : tr(STR_HOME_BROWSE);
  }
  const char* stateText(WidgetState state) const {
    switch (state) {
      case WidgetState::Off:
        return tr(STR_HOME_STATS_OFF);
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
  void paintFittedText(int font, int x, int y, const char* text, int width, bool black = true) const {
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
    renderer.drawText(font, x, y, caption, black);
  }
  void paintStatText(int font, int x, int y, const char* text, int width, int scale = 224) const {
    // Existing bitmap glyph scaling: no new font, buffer, filesystem or allocation.
    const auto* cursor = reinterpret_cast<const unsigned char*>(text);
    const int right = x + width;
    const int baseline = y + renderer.getFontAscenderSize(font) * scale / 256;
    while (*cursor) {
      const auto cp = utf8NextCodepoint(&cursor);
      int32_t advance = 0;
      int top = 0;
      if (!renderer.getCodepointMetrics(font, cp, EpdFontFamily::REGULAR, advance, top)) continue;
      const int pixels = (advance * scale / 256 + 8) / 16;
      if (x + pixels > right) break;
      x += renderer.drawScaledCodepoint(font, cp, EpdFontFamily::REGULAR, x, baseline, scale);
    }
  }
  bool widgetSelected(uint8_t index) const {
#if CROSSINK_APP_CAP_HOME_EDITOR
    if (editor.panel() != EditorAddRemove::Panel::Closed)
      return editor.panel() == EditorAddRemove::Panel::Selected ? editor.selected() == index : editor.focus() == index;
#endif
    return focus == index && !mappedInput.hasTouch();
  }
  void paintCalendarStats(uint8_t index, const StatsCard& statsValue, int line) const {
    const auto r = tileRect(index);
    const bool yearTile = widgets->page().widgets[index].kind == WidgetKind::Year;
    const bool expandedYear = yearTile && r.height >= 180;
    const int summaryWidth = expandedYear ? r.width - 20 : r.width * (yearTile ? 96 : 110) / 456 - 18;
    const int metadataScale = 160 * std::min(r.height, 108) / 108;
    char text[64];
    std::snprintf(text, sizeof(text),
                  yearTile ? (expandedYear ? tr(STR_HOME_STATS_DAYS_FORMAT) : "%llu") : tr(STR_HOME_STATS_HOURS_FORMAT),
                  (unsigned long long)(yearTile ? statsValue.best.value : statsValue.hours));
    paintStatText(BITTER_14_FONT_ID, r.x + 10, r.y + line + 2, text, summaryWidth, 288 * std::min(r.height, 108) / 108);
    if (!expandedYear)
      paintStatText(UI_10_FONT_ID, r.x + 10, r.y + 52 * std::min(r.height, 108) / 108,
                    yearTile ? tr(STR_HOME_STATS_DAYS_READ) : tr(STR_HOME_STATS_KNOWN_TOTAL), summaryWidth,
                    metadataScale);
    if (expandedYear && widgets->statsYear(index))
      std::snprintf(text, sizeof(text), tr(STR_HOME_STATS_YEAR_DETAIL_FORMAT), (unsigned long long)statsValue.hours,
                    (unsigned long long)widgets->statsYear(index)->unknown);
    else if (yearTile)
      std::snprintf(text, sizeof(text), tr(STR_HOME_STATS_HOURS_KNOWN_FORMAT), (unsigned long long)statsValue.hours);
    else if (statsValue.best.known)
      std::snprintf(text, sizeof(text), tr(STR_HOME_STATS_LAST_MONTH_FORMAT),
                    (unsigned long long)(statsValue.best.value / 3600));
    else
      std::snprintf(text, sizeof(text), "%s", tr(STR_HOME_STATS_LAST_MONTH_UNKNOWN));
    if (expandedYear) {
      const int width = std::min(r.width * 2 / 3 - 20, renderer.getTextWidth(UI_10_FONT_ID, text) * 160 / 256 + 12);
      paintStatText(UI_10_FONT_ID, r.x + r.width - 10 - width, r.y + line + 12, text, width, 160);
    } else
      paintStatText(UI_10_FONT_ID, r.x + 10, r.y + 74 * std::min(r.height, 108) / 108, text,
                    summaryWidth + (yearTile ? 0 : 8), yearTile ? metadataScale : metadataScale * 9 / 10);
    if (statsValue.stale) {
      std::snprintf(text, sizeof(text), tr(STR_HOME_STATS_SAVED_AGE_FORMAT), (unsigned long long)statsValue.ageDays);
      if (expandedYear)
        paintStatText(UI_10_FONT_ID, r.x + 10, r.y + 3 * line + 10, text, summaryWidth, 160);
      else
        paintStatText(UI_10_FONT_ID, r.x + r.width - 180, r.y + 5, text, 140, 160);
    }
    auto box = widgetBox(index);
    box.line = line;
    widgets::StatsMark mark;
    if (yearTile) {
      const auto* heat = widgets->statsYear(index);
      if (!heat) return;
      widgets::Year yearRenderer;
      for (unsigned day = 0; day < std::min<unsigned>(heat->count, 366); ++day) {
        if (!yearRenderer.heatCell(statsValue, *heat, box, day, mark) || mark.future) continue;
        const int x = r.x + mark.x, y = r.y + mark.y;
        if (!mark.known)
          renderer.drawRoundedRect(x, y, mark.width, mark.height, 1, mark.width / 2, true);
        else if (!mark.active)
          renderer.fillRect(x + mark.width / 2, y + mark.height / 2, 1, 1);
        else {
          renderer.fillRoundedRect(x, y, mark.width, mark.height, mark.width / 2, Black);
        }
      }
      static constexpr StrId labels[] = {StrId::STR_HOME_STATS_JAN, StrId::STR_HOME_STATS_MAR,
                                         StrId::STR_HOME_STATS_MAY, StrId::STR_HOME_STATS_JUL,
                                         StrId::STR_HOME_STATS_SEP, StrId::STR_HOME_STATS_NOV};
      for (unsigned month = 0; month < 12; month += 2) {
        if (yearRenderer.heatCell(statsValue, *heat, box, heat->monthStarts[month], mark))
          paintStatText(UI_10_FONT_ID, r.x + mark.x, r.y + (expandedYear ? 110 : 46) - line - 3,
                        I18N.get(labels[month / 2]), 42, expandedYear ? 160 : metadataScale);
      }
      if (expandedYear) {
        const int y = r.y + r.height - line - 5;
        renderer.fillRoundedRect(r.x + 10, y + 7, 5, 5, 2, Black);
        paintStatText(UI_10_FONT_ID, r.x + 22, y, tr(STR_HOME_STATS_LEGEND_READING), 75, 160);
        renderer.fillRect(r.x + 99, y + 9, 1, 1);
        paintStatText(UI_10_FONT_ID, r.x + 110, y, tr(STR_HOME_STATS_LEGEND_ZERO), 50, 160);
        renderer.drawRoundedRect(r.x + 165, y + 7, 5, 5, 1, 2, true);
        paintStatText(UI_10_FONT_ID, r.x + 177, y, tr(STR_HOME_STATS_LEGEND_UNKNOWN), 85, 160);
        paintStatText(UI_10_FONT_ID, r.x + 280, y, tr(STR_HOME_STATS_LEGEND_FUTURE), r.width - 290, 160);
      }
    } else {
      const auto* months = widgets->statsMonths(index);
      if (!months) return;
      widgets::Monthly monthlyRenderer;
      static constexpr StrId labels[] = {
          StrId::STR_HOME_STATS_MONTH_J, StrId::STR_HOME_STATS_MONTH_F, StrId::STR_HOME_STATS_MONTH_M,
          StrId::STR_HOME_STATS_MONTH_A, StrId::STR_HOME_STATS_MONTH_M, StrId::STR_HOME_STATS_MONTH_J,
          StrId::STR_HOME_STATS_MONTH_J, StrId::STR_HOME_STATS_MONTH_A, StrId::STR_HOME_STATS_MONTH_S,
          StrId::STR_HOME_STATS_MONTH_O, StrId::STR_HOME_STATS_MONTH_N, StrId::STR_HOME_STATS_MONTH_D};
      for (unsigned month = 0; month < 12; ++month) {
        if (!monthlyRenderer.monthColumn(statsValue, *months, box, month, mark)) continue;
        const int x = r.x + mark.x, y = r.y + mark.y;
        if (!mark.known)
          renderer.drawRect(x, y - 3, mark.width, 4);
        else if (mark.active)
          renderer.fillRect(x, y, mark.width, mark.height);
        else
          renderer.drawLine(x, y, x + mark.width, y);
        if (mark.known)
          std::snprintf(text, sizeof(text), "%llu", (unsigned long long)(months->totals[month].value / 3600));
        else
          std::snprintf(text, sizeof(text), "?");
        paintStatText(UI_10_FONT_ID, x, r.y + r.height - 2 * line - 5, text, mark.width + 3, metadataScale);
        paintStatText(UI_10_FONT_ID, x, r.y + r.height - line - 5, I18N.get(labels[month]), mark.width + 3,
                      metadataScale);
      }
    }
  }
  void paintStatsHeaderIcon(uint8_t index) const {
    const auto r = tileRect(index);
    const int edge = std::min(21, r.height / 5), left = r.x + r.width - edge - 10, top = r.y + 6;
    auto stroke = [&](int x1, int y1, int x2, int y2) {
      renderer.drawLine(left + x1 * edge / 24, top + y1 * edge / 24, left + x2 * edge / 24, top + y2 * edge / 24);
    };
    if (widgets->page().widgets[index].kind == WidgetKind::Today ||
        widgets->page().widgets[index].kind == WidgetKind::When) {
      renderer.drawRoundedRect(left + 2, top + 2, edge - 4, edge - 4, 1, (edge - 4) / 2, true);
      stroke(12, 6, 12, 12);
      stroke(12, 12, 16, 15);
    } else if (widgets->page().widgets[index].kind == WidgetKind::Goal) {
      renderer.drawRoundedRect(left + 2, top + 2, edge - 4, edge - 4, 1, (edge - 4) / 2, true);
      renderer.drawRoundedRect(left + 6, top + 6, edge - 12, edge - 12, 1, (edge - 12) / 2, true);
      stroke(12, 12, 22, 2);
    } else if (widgets->page().widgets[index].kind == WidgetKind::Records) {
      stroke(7, 3, 17, 3);
      stroke(17, 3, 17, 10);
      stroke(17, 10, 12, 16);
      stroke(12, 16, 7, 10);
      stroke(7, 10, 7, 3);
      stroke(7, 5, 3, 5);
      stroke(3, 5, 3, 10);
      stroke(3, 10, 8, 13);
      stroke(17, 5, 21, 5);
      stroke(21, 5, 21, 10);
      stroke(21, 10, 16, 13);
      stroke(12, 16, 12, 21);
      stroke(7, 22, 17, 22);
    } else if (widgets->page().widgets[index].kind == WidgetKind::Pages) {
      stroke(4, 4, 10, 4);
      stroke(10, 4, 12, 6);
      stroke(12, 6, 14, 4);
      stroke(14, 4, 20, 4);
      stroke(20, 4, 20, 19);
      stroke(20, 19, 14, 19);
      stroke(14, 19, 12, 21);
      stroke(12, 21, 10, 19);
      stroke(10, 19, 4, 19);
      stroke(4, 19, 4, 4);
      stroke(12, 6, 12, 21);
    } else if ((widgets->page().widgets[index].kind == WidgetKind::Year ||
                widgets->page().widgets[index].kind == WidgetKind::Forecast)) {
      stroke(3, 5, 21, 5);
      stroke(21, 5, 21, 21);
      stroke(21, 21, 3, 21);
      stroke(3, 21, 3, 5);
      stroke(3, 10, 21, 10);
      stroke(7, 2, 7, 8);
      stroke(17, 2, 17, 8);
      stroke(7, 14, 10, 14);
      stroke(14, 14, 17, 14);
    } else if (widgets->page().widgets[index].kind == WidgetKind::Streak) {
      // Fixed samples of the approved flame outline; flash-resident, no asset allocation.
      static constexpr int8_t outline[][2] = {
          {13, 2},  {14, 6},  {13, 9},  {10, 11}, {8, 10}, {8, 8},   {9, 6},   {5, 10},  {3, 14}, {5, 18}, {9, 23},
          {14, 23}, {18, 19}, {21, 15}, {20, 11}, {17, 8}, {17, 11}, {16, 12}, {14, 13}, {15, 8}, {14, 4}, {13, 2}};
      for (unsigned i = 1; i < sizeof(outline) / sizeof(outline[0]); ++i)
        stroke(outline[i - 1][0], outline[i - 1][1], outline[i][0], outline[i][1]);
    } else {
      stroke(3, 21, 22, 21);
      stroke(6, 17, 6, 9);
      stroke(12, 17, 12, 3);
      stroke(18, 17, 18, 12);
    }
  }
  static void formatStatsCount(uint64_t value, char (&out)[32]) {
    char reversed[27];
    unsigned n = 0, digits = 0;
    do {
      if (digits && digits % 3 == 0) reversed[n++] = ',';
      reversed[n++] = char('0' + value % 10);
      value /= 10;
      ++digits;
    } while (value);
    for (unsigned i = 0; i < n; ++i) out[i] = reversed[n - i - 1];
    out[n] = 0;
  }
  void paintDailyStats(uint8_t index, const StatsCard& statsValue, int line, int scale) const {
    const auto r = tileRect(index);
    const bool todayTile = widgets->page().widgets[index].kind == WidgetKind::Today;
    widgets::StatsSummary positions;
    widgets::Today todayRenderer;
    widgets::Pages pagesRenderer;
    auto box = widgetBox(index);
    box.line = line;
    if (!(todayTile ? todayRenderer.summaryPositions(statsValue, box, positions)
                    : pagesRenderer.summaryPositions(statsValue, box, positions)))
      return;
    char text[64], number[32];
    if (statsValue.value.known)
      formatStatsCount(statsValue.value.value, number);
    else
      std::snprintf(number, sizeof(number), "?");
    const int unit = std::min(r.height, 108), numberScale = 256 * unit / 108;
    const int metadataScale = scale * 5 / 7;
    const int numberWidth = std::min(r.width * 2 / 3, renderer.getTextWidth(BITTER_14_FONT_ID, number) * unit / 108);
    paintStatText(BITTER_14_FONT_ID, r.x + 10, r.y + positions.numberY, number, r.width * 2 / 3, numberScale);
    paintStatText(UI_10_FONT_ID, r.x + 16 + numberWidth, r.y + positions.numberY + 10 * unit / 108,
                  todayTile ? tr(STR_HOME_STATS_MIN) : tr(STR_HOME_STATS_TODAY_LABEL), r.width - numberWidth - 26,
                  scale);
    if (todayTile) {
      paintStatText(UI_10_FONT_ID, r.x + 10, r.y + positions.captionY,
                    statsValue.source == StatsCardSource::Account ? tr(STR_HOME_STATS_ACCOUNT_TIME)
                                                                  : tr(STR_HOME_STATS_ON_READER),
                    r.width - 20, metadataScale);
      const bool native = statsValue.source != StatsCardSource::Account;
      const auto other = native ? statsValue.account : statsValue.reader;
      if (other.known)
        std::snprintf(text, sizeof(text),
                      native ? tr(STR_HOME_STATS_ACCOUNT_MINUTES_FORMAT) : tr(STR_HOME_STATS_READER_MINUTES_FORMAT),
                      (unsigned long long)other.value);
      else
        std::snprintf(text, sizeof(text), "%s",
                      native ? tr(STR_HOME_STATS_ACCOUNT_UNKNOWN) : tr(STR_HOME_STATS_READER_UNKNOWN));
      paintStatText(UI_10_FONT_ID, r.x + 10, r.y + positions.sourceY, text, r.width - 20, metadataScale);
    } else {
      if (statsValue.best.known)
        formatStatsCount(statsValue.best.value, number);
      else
        std::snprintf(number, sizeof(number), "?");
      const int historyScale = (statsValue.stale ? 160 : 224) * unit / 108;
      const int historyWidth =
          std::min(r.width / 2, renderer.getTextWidth(UI_10_FONT_ID, number) * historyScale / 256 + 4);
      paintStatText(UI_10_FONT_ID, r.x + 10, r.y + positions.captionY, number, r.width / 2, historyScale);
      paintStatText(UI_10_FONT_ID, r.x + 14 + historyWidth,
                    r.y + positions.captionY + (statsValue.stale ? 0 : 4 * unit / 108),
                    tr(STR_HOME_STATS_SAVED_HISTORY), r.width - historyWidth - 24, metadataScale);
      paintStatText(UI_10_FONT_ID, r.x + 10, r.y + positions.sourceY, tr(STR_HOME_STATS_PAGE_VISITS_READER),
                    r.width - 20, metadataScale);
    }
    if (statsValue.stale) {
      std::snprintf(text, sizeof(text), tr(STR_HOME_STATS_SAVED_AGE_FORMAT), (unsigned long long)statsValue.ageDays);
      paintStatText(UI_10_FONT_ID, r.x + 10, r.y + positions.footerY, text, r.width - 20, metadataScale);
    }
  }
  void paintHoursStats(uint8_t index, const StatsCard& card) const {
    const auto* hours = widgets->statsHours(index);
    if (!hours) return;
    const auto r = tileRect(index);
    const int unit = std::min(r.height, 108), metadata = 160 * unit / 108;
    char text[64];
    if (hours->peakKnown)
      std::snprintf(text, sizeof(text), tr(STR_HOME_STATS_HOUR_FORMAT), unsigned(hours->peakHour));
    else
      std::snprintf(text, sizeof(text), "?");
    paintStatText(UI_10_FONT_ID, r.x + 10, r.y + 28 * unit / 108, text, r.width / 3, 224 * unit / 108);
    std::snprintf(text, sizeof(text), tr(STR_HOME_STATS_AFTER18_FORMAT), unsigned(hours->after18));
    paintStatText(UI_10_FONT_ID, r.x + r.width / 3, r.y + 32 * unit / 108, text, r.width * 2 / 3 - 10, metadata);
    widgets::StatsMark mark;
    for (unsigned hour = 0; hour < 24; ++hour) {
      if (!widgets::When::hourColumn(card, *hours, widgetBox(index), hour, mark)) continue;
      const int x = r.x + mark.x, y = r.y + mark.y;
      if (!mark.known)
        renderer.drawRect(x, y - 3, mark.width, 4);
      else if (mark.active) {
        if (mark.latestDay)
          renderer.fillRect(x, y, mark.width, mark.height);
        else
          renderer.drawRect(x, y, mark.width, mark.height);
      } else
        renderer.drawLine(x, y, x + mark.width, y);
      if (hour % 6 == 0) {
        std::snprintf(text, sizeof(text), "%02u", hour);
        paintStatText(UI_10_FONT_ID, x, r.y + (card.stale ? 73 : 90) * unit / 108, text, 24 * unit / 108, metadata);
      }
    }
    if (card.stale) {
      std::snprintf(text, sizeof(text), tr(STR_HOME_STATS_SAVED_AGE_FORMAT), (unsigned long long)card.ageDays);
      paintStatText(UI_10_FONT_ID, r.x + 10, r.y + 90 * unit / 108, text, r.width - 20, metadata);
    }
  }
  void paintRecordsStats(uint8_t index, const StatsCard& card) const {
    const auto* records = widgets->statsRecords(index);
    if (!records) return;
    const auto r = tileRect(index);
    const int unit = std::min(r.height, 108), metadata = 128 * unit / 108;
    static constexpr StrId labels[] = {StrId::STR_HOME_STATS_RECORD_PAGES, StrId::STR_HOME_STATS_RECORD_SESSION,
                                       StrId::STR_HOME_STATS_RECORD_DAY, StrId::STR_HOME_STATS_RECORD_BOOKS,
                                       StrId::STR_HOME_STATS_RECORD_STREAK};
    widgets::StatsMark cell;
    for (unsigned column = 0; column < 5; ++column) {
      if (!widgets::Records::recordCell(card, widgetBox(index), column, cell)) continue;
      char number[32], text[48];
      const auto value = records->values[column];
      if (!value.known)
        std::snprintf(text, sizeof(text), "?");
      else if (column == 1)
        std::snprintf(text, sizeof(text), tr(STR_HOME_STATS_SESSION_FORMAT), (unsigned long long)(value.value / 3600),
                      unsigned(value.value / 60 % 60));
      else if (column == 4)
        std::snprintf(text, sizeof(text), tr(STR_HOME_STATS_STREAK_SHORT_FORMAT), (unsigned long long)value.value);
      else {
        formatStatsCount(value.value, number);
        std::snprintf(text, sizeof(text), "%s", number);
      }
      paintStatText(UI_10_FONT_ID, r.x + cell.x, r.y + cell.y, text, cell.width, 224 * unit / 108);
      paintStatText(UI_10_FONT_ID, r.x + cell.x, r.y + 54 * unit / 108, I18N.get(labels[column]), cell.width, metadata);
    }
    renderer.drawLine(r.x + 10, r.y + 74 * unit / 108, r.x + r.width - 10, r.y + 74 * unit / 108);
    paintStatText(UI_10_FONT_ID, r.x + 10, r.y + 84 * unit / 108,
                  card.accountLinked ? tr(STR_HOME_STATS_RECORD_SOURCES) : tr(STR_HOME_STATS_RECORD_NATIVE),
                  r.width - 20, 160 * unit / 108);
    if (card.stale) {
      char text[64];
      std::snprintf(text, sizeof(text), tr(STR_HOME_STATS_SAVED_AGE_FORMAT), (unsigned long long)card.ageDays);
      paintStatText(UI_10_FONT_ID, r.x + r.width - 182, r.y + 8, text, 142, 160 * unit / 108);
    }
  }
  void paintGoalStats(uint8_t index, const StatsCard& card) const {
    const auto* goal = widgets->statsGoal(index);
    if (!goal) return;
    const auto r = tileRect(index);
    const int unit = std::min(r.height, 108);
    char number[32], text[80];
    formatStatsCount(card.value.value, number);
    const int width = std::min(r.width * 2 / 3, renderer.getTextWidth(BITTER_14_FONT_ID, number) * unit / 108);
    paintStatText(BITTER_14_FONT_ID, r.x + 10, r.y + 21 * unit / 108, number, r.width * 2 / 3, 256 * unit / 108);
    std::snprintf(text, sizeof(text), tr(STR_HOME_STATS_GOAL_TARGET_FORMAT), (unsigned long long)card.best.value);
    paintStatText(UI_10_FONT_ID, r.x + 16 + width, r.y + 31 * unit / 108, text, r.width - width - 26, 192 * unit / 108);
    widgets::StatsMark bar;
    if (widgets::Goal::progressBar(card, *goal, widgetBox(index), bar)) {
      renderer.drawRect(r.x + bar.x, r.y + bar.y, r.width - 20, bar.height);
      if (bar.width > 0) renderer.fillRect(r.x + bar.x, r.y + bar.y, bar.width, bar.height);
    }
    std::snprintf(text, sizeof(text),
                  goal->ahead ? tr(STR_HOME_STATS_GOAL_AHEAD_FORMAT) : tr(STR_HOME_STATS_GOAL_BEHIND_FORMAT),
                  unsigned(goal->year), unsigned(goal->paceTenths / 10), unsigned(goal->paceTenths % 10),
                  (unsigned long long)goal->difference);
    paintStatText(UI_10_FONT_ID, r.x + 10, r.y + 76 * unit / 108, text, r.width - 20, 144 * unit / 108);
    if (card.stale) {
      std::snprintf(text, sizeof(text), tr(STR_HOME_STATS_SAVED_AGE_FORMAT), (unsigned long long)card.ageDays);
      paintStatText(UI_10_FONT_ID, r.x + 10, r.y + 90 * unit / 108, text, r.width - 20, 144 * unit / 108);
    }
  }
  void paintForecastStats(uint8_t index, const StatsCard& card) const {
    const auto* detail = widgets->statsForecast(index);
    if (!detail) return;
    const auto r = tileRect(index);
    widgets::StatsSummary positions;
    if (!widgets::Forecast::summaryPositions(card, widgetBox(index), positions)) return;
    const int unit = std::min(r.height, 108);
    char text[80];
    if (card.hours)
      std::snprintf(text, sizeof(text), tr(STR_HOME_STATS_FORECAST_HOURS_FORMAT), (unsigned long long)card.hours);
    else
      std::snprintf(text, sizeof(text), tr(STR_HOME_STATS_FORECAST_MINUTES_FORMAT), unsigned(card.minutes));
    const int numberWidth = std::min(r.width * 2 / 3, renderer.getTextWidth(BITTER_14_FONT_ID, text) * unit / 108);
    paintStatText(BITTER_14_FONT_ID, r.x + 10, r.y + positions.numberY, text, r.width * 2 / 3, 256 * unit / 108);
    paintStatText(UI_10_FONT_ID, r.x + 16 + numberWidth, r.y + positions.numberY + 10 * unit / 108,
                  tr(STR_HOME_STATS_LEFT), r.width - numberWidth - 26, 224 * unit / 108);
    if (detail->dateKnown && detail->month >= 1 && detail->month <= 12 && detail->day >= 1 && detail->day <= 31) {
      static constexpr StrId weekdays[] = {StrId::STR_HOME_STATS_SUNDAY,   StrId::STR_HOME_STATS_MONDAY,
                                           StrId::STR_HOME_STATS_TUESDAY,  StrId::STR_HOME_STATS_WEDNESDAY,
                                           StrId::STR_HOME_STATS_THURSDAY, StrId::STR_HOME_STATS_FRIDAY,
                                           StrId::STR_HOME_STATS_SATURDAY};
      static constexpr StrId months[] = {
          StrId::STR_HOME_STATS_JAN, StrId::STR_HOME_STATS_FEB, StrId::STR_HOME_STATS_MAR, StrId::STR_HOME_STATS_APR,
          StrId::STR_HOME_STATS_MAY, StrId::STR_HOME_STATS_JUN, StrId::STR_HOME_STATS_JUL, StrId::STR_HOME_STATS_AUG,
          StrId::STR_HOME_STATS_SEP, StrId::STR_HOME_STATS_OCT, StrId::STR_HOME_STATS_NOV, StrId::STR_HOME_STATS_DEC};
      std::snprintf(text, sizeof(text), tr(STR_HOME_STATS_FINISH_DATE_FORMAT), I18N.get(weekdays[detail->weekday % 7]),
                    unsigned(detail->day), I18N.get(months[detail->month - 1]));
    } else
      std::snprintf(text, sizeof(text), "%s", tr(STR_HOME_STATS_FINISH_UNKNOWN));
    paintStatText(UI_10_FONT_ID, r.x + 10, r.y + positions.captionY, text, r.width - 20, 160 * unit / 108);
    const int titleWidth = r.width * 11 / 20 - 10;
    paintStatText(UI_10_FONT_ID, r.x + 10, r.y + positions.sourceY,
                  detail->title[0] ? detail->title.data() : tr(STR_HOME_STATS_CURRENT_BOOK), titleWidth,
                  144 * unit / 108);
    paintStatText(UI_10_FONT_ID, r.x + 10 + titleWidth, r.y + positions.sourceY, tr(STR_HOME_STATS_MEASURED_PACE),
                  r.width - titleWidth - 20, 144 * unit / 108);
    if (card.stale) {
      std::snprintf(text, sizeof(text), tr(STR_HOME_STATS_SAVED_AGE_FORMAT), (unsigned long long)card.ageDays);
      paintStatText(UI_10_FONT_ID, r.x + 10, r.y + positions.footerY, text, r.width - 20, 144 * unit / 108);
    }
  }
  void paintStatsWidget(uint8_t index) const {
    const auto r = tileRect(index);
    const auto kind = widgets->page().widgets[index].kind;
    const auto view = widgets->view(index);
    const auto* card = widgets->statsCard(index);
    const int font = UI_10_FONT_ID;
    constexpr int MainHeight = 108;
    const int unit = std::min(r.height, MainHeight);
    const int smallScale = 224 * unit / MainHeight;
    const int line = std::min(renderer.getLineHeight(font) * smallScale / 256, 18 * unit / MainHeight);
    renderer.drawRoundedRect(r.x, r.y, r.width, r.height, 1, 9, true);
    if (widgetSelected(index)) renderer.drawRoundedRect(r.x + 2, r.y + 2, r.width - 4, r.height - 4, 3, 8, true);
    renderer.beginTextClip(r.x + 8, r.y + 4, r.width - 16, r.height - 8);
    const bool calendarAge =
        card && card->stale && r.height < 180 && (kind == WidgetKind::Year || kind == WidgetKind::Monthly);
    paintStatText(font, r.x + 10, r.y + 5, widgetTitle(kind), r.width - (calendarAge ? 228 : 48), smallScale);
    paintStatsHeaderIcon(index);
    if (view.viewState != WidgetState::Known || !card) {
      const char* message = view.viewState == WidgetState::Empty
                                ? (kind == WidgetKind::Goal       ? tr(STR_HOME_STATS_GOAL_EMPTY)
                                   : kind == WidgetKind::Forecast ? tr(STR_HOME_STATS_FINISHED)
                                   : kind == WidgetKind::When     ? tr(STR_HOME_STATS_WHEN_EMPTY)
                                   : kind == WidgetKind::Records  ? tr(STR_HOME_STATS_RECORDS_EMPTY)
                                   : kind == WidgetKind::Today    ? tr(STR_HOME_STATS_TODAY_EMPTY)
                                   : kind == WidgetKind::Pages    ? tr(STR_HOME_STATS_PAGES_EMPTY)
                                   : kind == WidgetKind::Year     ? tr(STR_HOME_STATS_YEAR_EMPTY)
                                   : kind == WidgetKind::Monthly  ? tr(STR_HOME_STATS_MONTHLY_EMPTY)
                                   : kind == WidgetKind::Week     ? tr(STR_HOME_STATS_WEEK_EMPTY)
                                                                  : tr(STR_HOME_STATS_STREAK_EMPTY))
                            : view.viewState == WidgetState::Unsupported ? tr(STR_HOME_STATS_UNSUPPORTED)
                            : view.viewState == WidgetState::Oversize    ? tr(STR_HOME_STATS_OVERSIZE)
                            : kind == WidgetKind::Forecast && view.viewState == WidgetState::NotSaved
                                ? tr(STR_HOME_STATS_FORECAST_MISSING)
                                : stateText(view.viewState);
      paragraph(message, r.x + 10, r.y + line + 8, r.width - 20, 2);
      if (card && card->stale && view.viewState == WidgetState::Empty) {
        char age[64];
        std::snprintf(age, sizeof(age), tr(STR_HOME_STATS_SAVED_AGE_FORMAT), (unsigned long long)card->ageDays);
        paintStatText(font, r.x + 10, r.y + r.height - line - 5, age, r.width - 20, smallScale);
      }
      if (view.viewState == WidgetState::Oversize || view.viewState == WidgetState::Error)
        renderer.drawText(font, r.x + 10, r.y + r.height - line - 6, tr(STR_RETRY));
    } else if (kind == WidgetKind::Goal) {
      paintGoalStats(index, *card);
    } else if (kind == WidgetKind::Forecast) {
      paintForecastStats(index, *card);
    } else if (kind == WidgetKind::When) {
      paintHoursStats(index, *card);
    } else if (kind == WidgetKind::Records) {
      paintRecordsStats(index, *card);
    } else if (kind == WidgetKind::Today || kind == WidgetKind::Pages) {
      paintDailyStats(index, *card, line, smallScale);
    } else if (kind == WidgetKind::Year || kind == WidgetKind::Monthly) {
      paintCalendarStats(index, *card, line);
    } else {
      char text[80]{};
      if (kind == WidgetKind::Week) {
        std::snprintf(text, sizeof(text), tr(STR_HOME_STATS_TIME_FORMAT), (unsigned long long)card->hours,
                      (unsigned long long)card->minutes);
        paintStatText(font, r.x + 10, r.y + 28 * unit / MainHeight, text, r.width - 20, smallScale);
      } else {
        std::snprintf(text, sizeof(text), "%llu", (unsigned long long)card->value.value);
        const int numberY = (card->stale ? 24 : 28) * unit / MainHeight;
        const int numberWidth =
            std::min(r.width * 2 / 3, renderer.getTextWidth(BITTER_14_FONT_ID, text) * unit / MainHeight);
        paintStatText(BITTER_14_FONT_ID, r.x + 10, r.y + numberY, text, r.width * 2 / 3, 256 * unit / MainHeight);
        paintStatText(font, r.x + 16 + numberWidth, r.y + numberY + 10 * unit / MainHeight,
                      card->value.value == 1 ? tr(STR_STATS_DAY) : tr(STR_STATS_DAYS), r.width - numberWidth - 26,
                      smallScale);
      }
      if (kind == WidgetKind::Streak) {
        if (card->best.known)
          std::snprintf(text, sizeof(text), tr(STR_HOME_STATS_BEST_FORMAT), (unsigned long long)card->best.value);
        else
          std::snprintf(text, sizeof(text), "%s", tr(STR_HOME_STATS_BEST_UNKNOWN));
      } else if (card->stale)
        std::snprintf(text, sizeof(text), tr(STR_HOME_STATS_SAVED_AGE_FORMAT), (unsigned long long)card->ageDays);
      else
        std::snprintf(text, sizeof(text), "%s",
                      card->complete ? tr(STR_HOME_STATS_KNOWN_TIME) : tr(STR_HOME_STATS_PARTIAL));
      if (kind == WidgetKind::Streak)
        paintStatText(font, r.x + 10, r.y + (card->stale ? 75 : 89) * unit / MainHeight, text, r.width - 20,
                      160 * unit / MainHeight);
      else if (card->stale || !card->complete)
        paintStatText(font, r.x + 10, r.y + 48 * unit / MainHeight, text, r.width - 20, 160 * unit / MainHeight);
      if (kind == WidgetKind::Streak && card->stale) {
        std::snprintf(text, sizeof(text), tr(STR_HOME_STATS_SAVED_AGE_FORMAT), (unsigned long long)card->ageDays);
        paintStatText(font, r.x + 10, r.y + 91 * unit / MainHeight, text, r.width - 20, 144 * unit / MainHeight);
      }
      auto& drawing = statsDrawing;
      widgets::Week weekRenderer;
      widgets::Streak streakRenderer;
      auto box = widgetBox(index);
      box.line = line;
      const bool drawn = kind == WidgetKind::Week ? weekRenderer.layoutStats(*card, box, drawing)
                                                  : streakRenderer.layoutStats(*card, box, drawing);
      if (drawn && kind == WidgetKind::Week) {
        const char* labels[] = {tr(STR_HOME_STATS_SUN), tr(STR_HOME_STATS_MON), tr(STR_HOME_STATS_TUE),
                                tr(STR_HOME_STATS_WED), tr(STR_HOME_STATS_THU), tr(STR_HOME_STATS_FRI),
                                tr(STR_HOME_STATS_SAT)};
        for (unsigned day = 0; day < 7; ++day) {
          const auto* label = labels[(card->firstWeekday + day) % 7];
          const int scale = 128 * unit / MainHeight;
          const int width = renderer.getTextWidth(font, label) * scale / 256;
          paintStatText(font, r.x + drawing.marks[day].x + (drawing.marks[day].width - width) / 2,
                        r.y + 88 * unit / MainHeight, label, drawing.marks[day].width, scale);
        }
      }
      if (drawn)
        for (const auto& mark : drawing.marks) {
          const int x = r.x + mark.x, y = r.y + mark.y;
          if (mark.future) continue;
          if (drawing.dots) {
            if (mark.known && mark.active)
              renderer.fillRoundedRect(x, y, mark.width, mark.height, mark.width / 2, Black);
            else {
              renderer.drawRoundedRect(x, y, mark.width, mark.height, 1, mark.width / 2, true);
              if (!mark.known) paintStatText(font, x + 2, y - 1, "?", mark.width - 2, 128 * unit / MainHeight);
            }
          } else if (!mark.known)
            renderer.drawRect(x, y - 3, mark.width, 4);
          else if (mark.active) {
            if (mark.latestDay)
              renderer.fillRect(x, y, mark.width, mark.height);
            else
              renderer.drawRect(x, y, mark.width, mark.height);
          } else
            renderer.drawLine(x, y, x + mark.width, y);
        }
    }
    renderer.endTextClip();
  }
  void paintWidget(uint8_t index) const {
    const auto r = tileRect(index);
    if (r.width < 24 || r.height < 24) return;
    const auto& p = widgets->page().widgets[index];
    if (isStatsWidget(p.kind)) {
      paintStatsWidget(index);
      return;
    }
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
    const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const int height = renderer.getLineHeight(UI_10_FONT_ID) + 22;
    return Rect{safe.x + 24, safe.y + safe.height - 42 - (2 - row) * (height + 8), safe.width - 48, height};
  }
  void paintNotice() const {
    const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
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
      const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
      if (y >= safe.y && y < safe.y + 2 * renderer.getLineHeight(UI_10_FONT_ID) && x >= safe.x && x < safe.x + 100)
        notice.back();
    } else
      return true;
    requestUpdate();
    return true;
  }
#if CROSSINK_APP_CAP_HOME_EDITOR
  Rect textEditorRow(uint8_t row) const {
    const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const auto header = UITheme::getHeaderRect(renderer, UITheme::getInstance().getMetrics().headerHeight, safe);
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
    const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const auto header = UITheme::getHeaderRect(renderer, UITheme::getInstance().getMetrics().headerHeight, safe);
    const char* title = nullptr;
    const char* body = nullptr;
    char selectedTitle[96]{};
    if (panel == Panel::Selected) {
      const auto kind = widgets->page().widgets[editor.selected()].kind;
      std::snprintf(selectedTitle, sizeof(selectedTitle), tr(STR_HOME_REMOVE_CONFIRM_FORMAT), widgetTitle(kind));
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
    if (panel == Panel::Add) return widgetTitle(static_cast<WidgetKind>(row));
    const auto count = widgets->page().count;
    if (row < count) {
      const auto kind = widgets->page().widgets[row].kind;
      return widgetTitle(kind);
    }
    if (row >= count + 3) {
      const char* labels[] = {tr(STR_HOME_PAGE_PREVIOUS),    tr(STR_HOME_PAGE_NEXT), tr(STR_HOME_PAGE_ADD),
                              tr(STR_HOME_PAGE_NAME_ACTION), tr(STR_HOME_PAGE_LEFT), tr(STR_HOME_PAGE_RIGHT)};
      return labels[row - count - 3];
    }
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
    const int top = header.y + header.height + line * 3 + 28;
    const int capacity = std::max(1, (safe.y + safe.height - 48 - top) / height);
    const int first = editor.firstVisibleRow(uint8_t(capacity));
    return Rect{safe.x + 24, top + (int(index) - first) * height, safe.width - 48, height};
  }
  Rect editorPageButton(uint8_t index) const {
    const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
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
    if (widget >= 3) return widgetTitle(static_cast<WidgetKind>(widget));
    return widget == 0 ? tr(STR_HOME_PAGE_READING) : widget == 1 ? tr(STR_HOME_RECENT) : tr(STR_LIBRARY);
  }
  void paintPageKeyboard() const {
#if CROSSINK_APP_CAP_TOUCH
    if (!mappedInput.hasTouch()) return;
    namespace fui = freeink::ui;
    const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
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
        const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
        const auto header = UITheme::getHeaderRect(renderer, UITheme::getInstance().getMetrics().headerHeight, safe);
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
    const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const auto header = UITheme::getHeaderRect(renderer, UITheme::getInstance().getMetrics().headerHeight, safe);
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
      if (panel == Panel::Add &&
          (r.y < header.y + header.height + lineHeight * 3 + 28 || r.y + r.height > safe.y + safe.height - 48))
        continue;
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
      const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
      const auto header = UITheme::getHeaderRect(renderer, UITheme::getInstance().getMetrics().headerHeight, safe);
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
    const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const auto header = UITheme::getHeaderRect(renderer, UITheme::getInstance().getMetrics().headerHeight, safe);
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
    } else if (editor.panel() == Panel::Add && mappedInput.wasSwipe() != MappedInputManager::SwipeDir::None) {
      const auto direction = mappedInput.wasSwipe();
      if (direction == MappedInputManager::SwipeDir::Up)
        editor.stepEditor(1);
      else if (direction == MappedInputManager::SwipeDir::Down)
        editor.stepEditor(-1);
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
          if (editor.panel() == Panel::Add &&
              (r.y < header.y + header.height + renderer.getLineHeight(UI_10_FONT_ID) * 3 + 28 ||
               r.y + r.height > safe.y + safe.height - 48))
            continue;
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
  bool allowsReminderOverlay() const override {
    if (editorOnly || notice.visible()) return false;
#if CROSSINK_APP_CAP_HOME_EDITOR
    return editor.panel() == EditorAddRemove::Panel::Closed;
#else
    return true;
#endif
  }
  void onEnter() override {
    Activity::onEnter();
#if CROSSINK_SCALABLE_FONTS
    // Reuse the reader's fallible built-in serif loader on S3. It prepares the
    // existing font family once, outside paint, and supplies its UI fallback on
    // allocation failure. C3 keeps its flash-resident bitmap family.
    ensureScalableBuiltinFamily(renderer, 1);
#endif
    enterLocalLibrary(&defaultServerAdapter());
    if (std::string_view(Layout::deviceClass()) != "x3") bindDeviceBridge(&deviceRuntime());
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
    bindDeviceBridge(nullptr);
    retireDeviceRuntime();
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
      if (deviceStatusWanted() && workDeviceRuntime({this, deviceNavigation})) notifyDevicePublication();
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
    if (takeCatalogRequest()) {
      auto screen = createCatalogScreen(renderer, mappedInput, library);
      if (screen)
        startActivityForResult(std::move(screen), [this](const ActivityResult&) {
          if (widgets) widgets->enter(widgets->page().page);
          requestUpdate();
        });
      return;
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
    const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const auto header = UITheme::getHeaderRect(renderer, UITheme::getInstance().getMetrics().headerHeight, safe);
#if CROSSINK_APP_CAP_HOME_EDITOR
    const bool editing = editor.panel() != EditorAddRemove::Panel::Closed;
#else
    constexpr bool editing = false;
#endif
    const char* title = editing && widgets->page().textOnly   ? tr(STR_HOME_WIDGETS)
                        : widgets && widgets->page().title[0] ? widgets->page().title.data()
                                                              : tr(STR_HOME);
#if CROSSINK_APP_CAP_HOME_EDITOR
    if (editing && !widgets->page().textOnly && editor.panel() != EditorAddRemove::Panel::Select)
      title = tr(STR_HOME_EDIT);
    if (editing && editor.draftPage()) {
      const auto kind = editor.draftPage()->widgets[editor.selected()].kind;
      title = widgetTitle(kind);
    }
#endif
    int titleWidth = header.width - 24;
#if CROSSINK_APP_CAP_HOME_EDITOR
    if (canEdit()) titleWidth -= 80;
#endif
    renderer.beginTextClip(header.x + 12, header.y, titleWidth, header.height);
    const bool compact = widgets && !widgets->page().textOnly && widgets->page().rows == 6;
    renderer.drawText(BITTER_14_FONT_ID, header.x + 12, compact ? std::max(0, safe.y - 6) : header.y + 4, title, true,
                      EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, header.x + 12,
                      compact ? safe.y + 24 : header.y + renderer.getLineHeight(BITTER_14_FONT_ID) + 6,
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
      if (!widgets->page().textOnly && widgets->page().rows == 6)
        renderer.drawText(UI_10_FONT_ID, safe.x + safe.width - 64, safe.y + 28, text);
      else
        renderer.drawText(UI_10_FONT_ID, safe.x + 12, safe.y + safe.height - 30, text);
    } else {
      UITheme::drawCenteredText(renderer, safe, UI_12_FONT_ID, header.y + header.height + 24,
                                lastAvailable ? tr(STR_CONTINUE_READING) : tr(STR_NO_RECENT_BOOKS));
    }
    const char* action = lastAvailable && (!widgets || !widgets->page().revision) ? tr(STR_OPEN) : "";
    if (widgets && focus < widgets->page().count) {
      const auto state = widgets->view(focus).viewState;
      if (!isStatsWidget(widgets->page().widgets[focus].kind) && state == WidgetState::Known &&
          focusRow < widgets->widgetRows(focus, widgetBox(focus)))
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
