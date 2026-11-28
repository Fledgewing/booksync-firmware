// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceListScreen.h"

#include <I18n.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>

#include "AppCapabilities.h"
#include "ArtworkCache.h"
#include "activities/ActivityManager.h"
#include "booksync/services/LocalLibrary.h"
#include "booksync/system/NetworkPhase.h"
#include "booksync/ui/catalog/SourceList.h"
#include "components/UITheme.h"
#include "fontIds.h"
namespace booksync {
#if CROSSINK_APP_CAP_HOME_EDITOR && !CROSSINK_APP_CAP_HOME_EDITOR_SETTINGS
namespace {
class SavedSourceActivity final : public Activity {
  Library& library_;
  SourceList list_;
  WindowSource source_;
  uint8_t focus_ = 0;
  uint16_t capacity_ = 1;
  bool actionFailed_ = false, painted_ = false, closing_ = false;
#if CROSSINK_APP_CAP_HOME_EDITOR && !CROSSINK_APP_CAP_HOME_EDITOR_SETTINGS
  ArtworkCache artwork_;
#endif
  Rect sourceArea() const { return UITheme::getInstance().getScreenSafeArea(renderer, true, false); }
  int sourceTop() const {
    const auto area = sourceArea();
    return UITheme::getHeaderRect(renderer, UITheme::getInstance().getMetrics().headerHeight, area).y +
           UITheme::getInstance().getMetrics().headerHeight + 8;
  }
  Rect sourceRowRect(uint8_t index) const {
    const auto area = sourceArea();
    return Rect{area.x + 12, sourceTop() + index * 54, area.width - 24, 54};
  }
  Rect sourceFooterRect(uint8_t index) const {
    const auto area = sourceArea();
    return Rect{area.x + 12 + index * (area.width - 120) / 2, area.y + area.height - 54, 96, 50};
  }
  const char* sourceHeading() const {
    switch (source_) {
      case WindowSource::AlsoReading:
        return tr(STR_HOME_ALSO_READING);
      case WindowSource::Recent:
        return tr(STR_HOME_RECENT);
      case WindowSource::WantToRead:
        return tr(STR_HOME_WANT_TO_READ);
      case WindowSource::AllFinished:
        return tr(STR_HOME_ALL_FINISHED);
      case WindowSource::PickBackUp:
        return tr(STR_HOME_PICK_BACK_UP);
      case WindowSource::UpNext:
        return tr(STR_HOME_UP_NEXT_IN_SERIES);
      case WindowSource::FinishedYear:
        return tr(STR_HOME_FINISHED_YEAR);
      case WindowSource::Unopened:
        return tr(STR_HOME_UNOPENED_WIDGET);
      default:
        return tr(STR_HOME_ON_DEVICE);
    }
  }
  uint8_t sourceTargets() const {
    const auto view = list_.sourceView();
    if (view.viewState == WidgetState::Error || view.viewState == WidgetState::Oversize) return 2;
    return view.count + 1 + (list_.hasSourcePrevious() ? 1 : 0) + (list_.hasSourceNext() ? 1 : 0);
  }
  void chooseSourceTarget(uint8_t target) {
    const auto view = list_.sourceView();
    if (target < view.count) {
      actionFailed_ = list_.openSourceRow(target) != CommandStatus::Accepted;
      return;
    }
    if (target == view.count) {
      list_.retireSourceList();
      closing_ = true;
      finish();
      return;
    }
    if (view.viewState == WidgetState::Error || view.viewState == WidgetState::Oversize)
      list_.retrySourcePage();
    else if (list_.hasSourcePrevious() && target == view.count + 1)
      list_.previousSourcePage();
    else
      list_.nextSourcePage();
    focus_ = 0;
    actionFailed_ = false;
    painted_ = false;
  }
  void sourceText(int x, int y, int width, int height, const char* text, bool bold = false) const {
    renderer.beginTextClip(x, y, width, height);
    renderer.drawText(SMALL_FONT_ID, x, y, text, true, bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    renderer.endTextClip();
  }
  void sourceFooter(uint8_t position, const char* text, uint8_t target) const {
    const auto r = sourceFooterRect(position);
    if (!mappedInput.hasTouch() && focus_ == target) renderer.drawRoundedRect(r.x, r.y, r.width, r.height, 2, 6, true);
    sourceText(r.x + 8, r.y + 14, r.width - 16, 24, text, true);
  }

 public:
  SavedSourceActivity(GfxRenderer& r, MappedInputManager& i, Library& library, WindowSource source)
      : Activity("SavedSource", r, i), library_(library), list_(library), source_(source) {}
  void onEnter() override {
    Activity::onEnter();
    const auto area = sourceArea();
    capacity_ = std::clamp((area.y + area.height - 62 - sourceTop()) / 54, 1, 12);
    list_.enter(source_, capacity_);
#if CROSSINK_APP_CAP_HOME_EDITOR && !CROSSINK_APP_CAP_HOME_EDITOR_SETTINGS
#ifdef SIMULATOR
    artwork_.initArtworkCache(MemoryPool::None);
#else
    artwork_.initArtworkCache(MemoryPool::Psram);
#endif
#endif
    requestUpdate();
  }
  void onExit() override {
    list_.retireSourceList();
#if CROSSINK_APP_CAP_HOME_EDITOR && !CROSSINK_APP_CAP_HOME_EDITOR_SETTINGS
    artwork_.resetArtworkCache();
#endif
    Activity::onExit();
  }
  void loop() override {
    {
      RenderLock lock;
      if (!networkAdmission().busy()) workLocalLibrary();
      if (list_.pollSourceList()) {
        focus_ = std::min<uint8_t>(focus_, sourceTargets() - 1);
        painted_ = false;
#if CROSSINK_APP_CAP_HOME_EDITOR && !CROSSINK_APP_CAP_HOME_EDITOR_SETTINGS
        artwork_.beginArtworkPublication();
        const auto retainedView=list_.sourceView();
        if(retainedView.viewState==WidgetState::Known)
          for(uint16_t i=0;i<retainedView.count;++i)artwork_.retainArtworkKey(retainedView.rows[i].key);
        artwork_.endArtworkPublication();
#endif
        requestUpdate();
      }
#if CROSSINK_APP_CAP_HOME_EDITOR && !CROSSINK_APP_CAP_HOME_EDITOR_SETTINGS
      if (painted_) {
        const auto view = list_.sourceView();
        bool changed = false;
        if (view.viewState == WidgetState::Known)
          for (uint16_t i = 0; i < view.count; ++i) changed |= artwork_.pollArtworkKey(library_, view.rows[i].key);
        if (changed) requestUpdate();
      }
#endif
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        list_.retireSourceList();
        closing_ = true;
        finish();
        return;
      }
      const int step = mappedInput.wasReleased(mappedInput.menuButton(MappedInputManager::Button::Down)) ? 1
                       : mappedInput.wasReleased(mappedInput.menuButton(MappedInputManager::Button::Up)) ? -1
                                                                                                         : 0;
      if (step) {
        focus_ = (focus_ + sourceTargets() + step) % sourceTargets();
        requestUpdate();
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        chooseSourceTarget(focus_);
        requestUpdate();
      }
      int x = 0, y = 0;
      if (mappedInput.wasScreenTapped(x, y)) {
        const auto view = list_.sourceView();
        for (uint8_t i = 0; i < view.count; ++i) {
          const auto r = sourceRowRect(i);
          if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height) chooseSourceTarget(i);
        }
        for (uint8_t i = 0; i < 3; ++i) {
          const auto r = sourceFooterRect(i);
          if (x < r.x || x >= r.x + r.width || y < r.y || y >= r.y + r.height) continue;
          if (i == 0)
            chooseSourceTarget(view.count);
          else if (i == 1 && list_.hasSourcePrevious())
            chooseSourceTarget(view.count + 1);
          else if (i == 2 && list_.hasSourceNext())
            chooseSourceTarget(view.count + 1 + (list_.hasSourcePrevious() ? 1 : 0));
          else if (i == 2 && (view.viewState == WidgetState::Error || view.viewState == WidgetState::Oversize))
            chooseSourceTarget(1);
        }
        requestUpdate();
      }
    }
    if (closing_) return;
    std::string path;
    if (takeIndexedOpenRequest(path)) activityManager.goToReader(path);
  }
  void render(RenderLock&&) override {
    renderer.clearScreen();
    const auto area = sourceArea();
    const auto header = UITheme::getHeaderRect(renderer, UITheme::getInstance().getMetrics().headerHeight, area);
    sourceText(header.x + 12, header.y + 4, header.width - 24, 26, sourceHeading(), true);
    const auto view = list_.sourceView();
    char caption[48]{};
    if (view.viewState == WidgetState::Known || view.viewState == WidgetState::Empty) {
      std::snprintf(caption, sizeof(caption), tr(STR_HOME_SOURCE_COUNT_FORMAT), static_cast<unsigned long>(view.total));
      sourceText(header.x + 12, header.y + 32, header.width - 24, 24, caption);
    }
    if (view.viewState == WidgetState::Known) {
      for (uint8_t i = 0; i < view.count; ++i) {
        const auto r = sourceRowRect(i);
        if (!mappedInput.hasTouch() && focus_ == i) renderer.drawRoundedRect(r.x, r.y, r.width, r.height, 2, 4, true);
        int textX = r.x + 8, textWidth = r.width - 16;
#if CROSSINK_APP_CAP_HOME_EDITOR && !CROSSINK_APP_CAP_HOME_EDITOR_SETTINGS
        renderer.drawRect(r.x + 8, r.y + 3, 32, 48, 1, true);
        if (const auto* bitmap = artwork_.bitmapForArtwork(view.rows[i].key)) {
          const int stride = (bitmap->width + 7) / 8;
          for (int py = 0; py < 48; ++py)
            for (int px = 0; px < 32; ++px) {
              const int sx = px * bitmap->width / 32, sy = py * bitmap->height / 48;
              renderer.drawPixel(r.x + 8 + px, r.y + 3 + py,
                                 !(bitmap->pixels[sy * stride + sx / 8] & (0x80 >> (sx % 8))));
            }
        }
        textX += 42;
        textWidth -= 42;
#endif
        sourceText(textX, r.y + 7, textWidth, 26, view.rows[i].title.data());

        if (actionFailed_ && focus_ == i) sourceText(r.x + 8, r.y + 30, r.width - 16, 22, tr(STR_HOME_UNAVAILABLE));
      }
    } else {
      const char* text = view.viewState == WidgetState::Loading       ? tr(STR_HOME_LOADING)
                         : view.viewState == WidgetState::Empty       ? tr(STR_HOME_NO_BOOKS)
                         : view.viewState == WidgetState::NotSaved    ? tr(STR_HOME_NOT_SAVED)
                         : view.viewState == WidgetState::Off         ? tr(STR_HOME_SOURCE_OFF)
                         : view.viewState == WidgetState::Unsupported ? tr(STR_HOME_UNAVAILABLE)
                                                                      : tr(STR_HOME_WIDGET_ERROR);
      sourceText(area.x + 12, sourceTop() + 20, area.width - 24, 54, text);
    }
    sourceFooter(0, tr(STR_BACK), view.count);
    if (list_.hasSourcePrevious()) sourceFooter(1, tr(STR_HOME_PAGE_PREVIOUS), view.count + 1);
    if (list_.hasSourceNext())
      sourceFooter(2, tr(STR_HOME_PAGE_NEXT), view.count + 1 + (list_.hasSourcePrevious() ? 1 : 0));
    if (view.viewState == WidgetState::Error || view.viewState == WidgetState::Oversize)
      sourceFooter(2, tr(STR_RETRY), 1);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    painted_ = true;
  }
};
}  // namespace
#endif
std::unique_ptr<Activity> createSourceListScreen(GfxRenderer& renderer, MappedInputManager& input, Library& library,
                                                 WindowSource source) {
#if CROSSINK_APP_CAP_HOME_EDITOR && !CROSSINK_APP_CAP_HOME_EDITOR_SETTINGS
  auto activity = makeUniqueNoThrow<SavedSourceActivity>(renderer, input, library, source);
  if (!activity) LOG_ERR("BSL", "Cannot allocate saved-source activity");
  return activity;
#else
  LOG_ERR("BSCL","Saved-source widget destination requires X4");return nullptr;
#endif
}
}  // namespace booksync
