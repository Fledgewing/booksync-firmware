// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "activities/Activity.h"
#include "booksync/services/Layout.h"
#include "booksync/services/widgets/Records.h"
#include "booksync/services/widgets/When.h"
#include "booksync/system/Bootstrap.h"
extern GfxRenderer renderer;
extern MappedInputManager mappedInputManager;
namespace recordsWidgetScenario {
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
    values.hours.fill(0);
    values.hoursAvailable = true;
    values.hoursKnown = 0xffffff;
    static constexpr uint16_t minutes[] = {0, 0, 0, 0, 0, 0, 2, 3, 2, 1, 2, 3, 4, 2, 1, 2, 2, 3, 6, 10, 18, 14, 7, 1};
    for (unsigned h = 0; h < 24; ++h) values.hours[h] = uint64_t(minutes[h]) * 60;
    values.recordedPages = {48210, true};
    values.longestSession = {11520, true};
    values.bestDayPages = {412, true};
    values.finished = {18, true};
    values.bestStreak = {12, true};
    values.hoursCoverage = values.recordsCoverage =
        values.streakCoverage = {ReadingOrigin::ThisReader, {}, 1, 100, true, true};
    values.finishedCoverage = {ReadingOrigin::Account, {7, 2}, 1, 100, true, true};
    StatsProjection options;
    options.provenance.owner = 7;
    options.provenance.accountEpoch = 2;
    if (scene == 1 || scene == 9) {
      values.hours.fill(0);
      values.recordedPages.value = values.longestSession.value = values.bestDayPages.value = values.finished.value =
          values.bestStreak.value = 0;
    }
    if (scene == 2) {
      values.hoursKnown = 0;
      values.recordedPages = values.longestSession = values.bestDayPages = values.finished = values.bestStreak = {};
    }
    if (scene == 3) options.enabled = false;
    if (scene == 4) options.availability = StatsCardState::Unsupported;
    if (scene == 5 || scene == 9) {
      options.stale = true;
      options.ageDays = 2;
    }
    if (scene == 7) options.availability = StatsCardState::Unavailable;
    if (scene == 8) {
      values.hoursKnown = 1u << 20;
      values.bestDayPages = {};
      values.finished = {0, true};
    }
    if (scene == 10) {
      values.hoursKnown = 1u << 20;
      values.recordedPages = values.longestSession = values.bestDayPages = values.bestStreak = {};
    }
    projectWhen(values, options, stats.cards[unsigned(StatsWidget::When)], stats.hours);
    projectRecords(values, options, stats.cards[unsigned(StatsWidget::Records)], stats.records);
    if (scene == 6) ++stats.cards[unsigned(StatsWidget::When)].bytes;
    return true;
  }
};
inline bool tick() {
  const char* output = std::getenv("BOOKSYNC_RECORDS_WIDGET_CAPTURES");
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
      fixture.page.widgets[0] = {WidgetKind::When, WindowSource::Unknown, 0, 0, 1, 1};
      fixture.page.widgets[1] = {WidgetKind::Records, WindowSource::Unknown, 0, 1, 2, 1};
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
    LOG_INF("BSW", "BookSync F40w hours/records PASS: eleven states, one batch, X3 off");
    std::fflush(nullptr);
    std::_Exit(0);
  }
  return true;
}
}  // namespace recordsWidgetScenario
