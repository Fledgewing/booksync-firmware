// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "activities/Activity.h"
#include "booksync/services/Layout.h"
#include "booksync/services/widgets/Pages.h"
#include "booksync/services/widgets/Today.h"
#include "booksync/system/Bootstrap.h"
extern GfxRenderer renderer;
extern MappedInputManager mappedInputManager;
namespace dailyWidgetScenario {
using namespace booksync;
class Fixture final : public Library {
 public:
  HomePage page{};
  LibraryWindow windowValue{};
  StatsWidgetBatch stats{};
  unsigned scene = 0, batches = 0;
  mutable unsigned windowReads = 0, statsReads = 0;
  Capabilities capabilities() const override { return {}; }
  Snapshot snapshot() const override { return {}; }
  AccountView accountView() const override {
    AccountView value;
    value.ticket = {7, 2};
    return value;
  }
  CommandStatus submit(Intent) override { std::_Exit(6); }
  HomePage homePage() const override { return page; }
  bool requestHomePage(uint8_t) override { return true; }
  Subscription subscribe() const override { return {windowValue.publication, 1, true}; }
  void readWindow(LibraryWindow& out) const override {
    ++windowReads;
    out = windowValue;
  }
  void readStatsWidgets(StatsWidgetBatch& out) const override {
    ++statsReads;
    out = stats;
  }
  bool requestBatch(WindowBatchRequest batch) override {
    ++batches;
    ++windowValue.publication;
    windowValue.windowCount = batch.count;
    for (unsigned i = 0; i < batch.count; ++i) {
      auto& span = windowValue.windows[i];
      span.request = batch.windows[i];
      span.availability = Availability::Ready;
      span.bytes = sizeof(WindowSpan);
    }
    stats.requested = batch.statsWidgets;
    static StatsSnapshot values;
    values.days.fill({0, true});
    values.firstDay = 20784 - ReadingStats::Days + 1;  // Synthetic Friday.
    static constexpr uint16_t Minutes[] = {0, 0, 20, 10, 25, 30, 15};
    for (unsigned i = 0; i < 7; ++i) values.days[358 + i] = {uint64_t(Minutes[i]) * 60, true};
    values.streak = {5, true};
    values.bestStreak = {12, true};
    values.daysCoverage = values.streakCoverage = {ReadingOrigin::ThisReader, {}, 1, 100, true, true};
    values.nativeToday = {1440, true};
    values.accountToday = {};
    values.todayPages = {36, true};
    values.recordedPages = {1240, true};
    values.nativeTodayCoverage = values.recordsCoverage = {ReadingOrigin::ThisReader, {}, 1, 100, true, true};
    values.accountTodayCoverage = {ReadingOrigin::Account, {7, 2}, 1, 100, true, true};
    StatsProjection options;
    options.provenance.owner = 7;
    options.provenance.accountEpoch = 2;
    if (scene == 1) {
      values.days.fill({0, true});
      values.streak = {0, true};
      values.bestStreak = {0, true};
    }
    if (scene == 9) {
      values.days.fill({0, true});
      values.streak = values.bestStreak = {0, true};
      options.stale = true;
      options.ageDays = 2;
    }
    if (scene == 8) {
      values.days.fill({0, true});
      values.streak = {0, true};
      values.bestStreak = {12, true};
    }
    if (scene == 2) {
      values.days.fill({});
      values.streak = {};
    }
    if (scene == 3) options.enabled = false;
    if (scene == 4) options.availability = StatsCardState::Unsupported;
    if (scene == 5) {
      options.stale = true;
      options.ageDays = 2;
    }
    if (scene == 7) options.availability = StatsCardState::Unavailable;
    if (scene == 1 || scene == 9) {
      values.nativeToday.value = values.todayPages.value = values.recordedPages.value = 0;
    }
    if (scene == 2) values.nativeToday = values.accountToday = values.todayPages = values.recordedPages = {};
    if (scene == 8) {
      values.nativeToday = {0, true};
      values.accountToday = {720, true};
      values.todayPages = {0, true};
    }
    if (scene == 10) {
      values.nativeToday = {};
      values.accountToday = {720, true};
    }
    projectToday(values, options, stats.cards[unsigned(StatsWidget::Today)]);
    projectPages(values, options, stats.cards[unsigned(StatsWidget::Pages)]);
    if (scene == 6) ++stats.cards[unsigned(StatsWidget::Today)].bytes;
    return true;
  }
};
inline bool tick() {
  const char* output = std::getenv("BOOKSYNC_DAILY_WIDGET_CAPTURES");
  if (!output) return false;
  mappedInputManager.simulatorClearInputFrame();
  static Fixture fixture;
  static unsigned frame = 0, scene = 0;
  if (frame++ == 0) {
    fixture.page = {};
    fixture.page.rows = 6;
    fixture.page.revision = 1;  // Values change; the old activity may poll until queued replacement runs.
    fixture.page.textOnly = std::string_view(Layout::deviceClass()) == "x3";
    if (!fixture.page.textOnly) {
      fixture.page.count = 2;
      fixture.page.widgets[0] = {WidgetKind::Today, WindowSource::Unknown, 0, 0, 1, 1};
      fixture.page.widgets[1] = {WidgetKind::Pages, WindowSource::Unknown, 1, 0, 1, 1};
    }
    fixture.scene = scene;
    fixture.batches = fixture.windowReads = fixture.statsReads = 0;
    showHomeScene(renderer, mappedInputManager, fixture);
  }
  if (scene == 0 && !fixture.page.textOnly) {
    if (frame == 15 || frame == 25) mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Confirm);
    if (frame == 20)
      mappedInputManager.simulatorInjectRelease(mappedInputManager.menuButton(MappedInputManager::Button::Down));
  }
  if (frame < 35) return true;
  if (activityManager.requestUpdateAndWait() != RequestUpdateResult::Rendered) std::_Exit(2);
  {
    RenderLock lock;
    const bool x3 = fixture.page.textOnly;
    if (fixture.batches != (x3 ? 0 : 1) || fixture.windowReads != (x3 ? 0 : 1) || fixture.statsReads != (x3 ? 0 : 1)) {
      LOG_ERR("BSW", "Stats scene %u has batch/window/stats counts %u/%u/%u", scene, fixture.batches,
              fixture.windowReads, fixture.statsReads);
      std::fflush(nullptr);
      std::_Exit(3);
    }
    std::filesystem::create_directories(output);
    const char* names[] = {"known",    "empty", "not-saved",  "off",         "unsupported", "stale",
                           "oversize", "error", "known-zero", "stale-empty", "account-only"};
    const auto path = std::filesystem::path(output) / (std::string(names[scene]) + ".pgm");
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) std::_Exit(4);
    std::fprintf(file, "P5\n%d %d\n255\n", renderer.getScreenWidth(), renderer.getScreenHeight());
    for (int y = 0; y < renderer.getScreenHeight(); ++y)
      for (int x = 0; x < renderer.getScreenWidth(); ++x) std::fputc(renderer.isPixelBlack(x, y) ? 0 : 255, file);
    std::fclose(file);
  }
  frame = 0;
  if (++scene == 11) {
    LOG_INF("BSW", "BookSync F40w today/pages PASS: eleven states, one batch, X3 off");
    std::fflush(nullptr);
    std::_Exit(0);
  }
  return true;
}
}  // namespace dailyWidgetScenario
