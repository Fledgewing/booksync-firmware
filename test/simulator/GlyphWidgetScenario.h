// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "activities/Activity.h"
#include "booksync/services/Layout.h"
#include "booksync/services/widgets/Glyph.h"
#include "booksync/system/Bootstrap.h"
#include "booksync/ui/WidgetHost.h"
extern GfxRenderer renderer;
extern MappedInputManager mappedInputManager;
namespace glyphWidgetScenario {
using namespace booksync;
class Fixture final : public Library {
 public:
  HomePage page{};
  LibraryWindow publication{};
  TimeWidgetBatch time{};
  unsigned batches = 0, commands = 0;
  mutable unsigned reads = 0, timeReads = 0;
  Capabilities capabilities() const override { return {}; }
  Snapshot snapshot() const override { return {}; }
  HomePage homePage() const override { return page; }
  bool requestHomePage(uint8_t) override { return true; }
  Subscription subscribe() const override { return {publication.publication, 1, true}; }
  CommandStatus submit(Intent) override {
    ++commands;
    return CommandStatus::Unavailable;
  }
  void readWindow(LibraryWindow& out) const override {
    ++reads;
    out = publication;
  }
  void readTimeWidgets(TimeWidgetBatch& out) const override {
    ++timeReads;
    out = time;
  }
  bool requestBatch(WindowBatchRequest batch) override {
    ++batches;
    ++publication.publication;
    publication.count = 0;
    publication.windowCount = batch.count;
    time.requested = batch.timeWidgets;
    for (unsigned i = 0; i < batch.count; ++i) {
      auto& span = publication.windows[i];
      span = {};
      span.request = batch.windows[i];
      span.first = publication.count;
      span.bytes = sizeof(WindowSpan);
      span.availability =
          span.request.source == WindowSource::Unknown ? Availability::Unsupported : Availability::Ready;
      if (span.availability == Availability::Ready) {
        span.total = 7;
        span.count = 1;
        span.bytes += sizeof(BookRow);
        auto& row = publication.rows[publication.count++];
        row = {};
        std::snprintf(row.title.data(), row.title.size(), "Synthetic sibling book");
      }
    }
    return true;
  }
  void scene(unsigned sceneIndex) {
    page = {};
    page.revision = 1;
    page.cols = 2;
    page.rows = 6;
    page.textOnly = std::string_view(Layout::deviceClass()) == "x3";
    page.count = page.textOnly ? 1 : 2;
    if (!page.textOnly) {
      page.widgets[0] = {WidgetKind::Glyph, WindowSource::Unknown, 0, 0, 1, 1};
    }
    page.widgets[page.count - 1] = {WidgetKind::Browse, WindowSource::OnDevice, 0, 1, 2, 1};
    static constexpr TimeCardState states[] = {
        TimeCardState::Ready,       TimeCardState::Empty, TimeCardState::NotSaved,    TimeCardState::Off,
        TimeCardState::Unsupported, TimeCardState::Ready, TimeCardState::Unavailable, TimeCardState::Ready};
    const ClockStatus clock{ClockEvidence::Synchronized, 1795789920, 0, {2026, 11, 27, 5, 14, 32, 0}};
    const TimeProjection options{states[sceneIndex], sceneIndex == 7 ? 2u : 0u, true, sceneIndex == 7};
    projectGlyphWidget(clock, sceneIndex != 4, options, time.glyph);
    if (sceneIndex == 5) {
      ++time.glyph.status.bytes;
    }
    batches = reads = timeReads = commands = 0;
  }
};
inline bool tick() {
  const char* output = std::getenv("BOOKSYNC_GLYPH_WIDGET_CAPTURES");
  if (!output) return false;
  mappedInputManager.simulatorClearInputFrame();
  static Fixture fixture;
  static unsigned frame = 0, scene = 0;
  if (frame++ == 0) {
    fixture.scene(scene);
    showHomeScene(renderer, mappedInputManager, fixture);
  }
  if (frame < 40) return true;
  if (activityManager.requestUpdateAndWait() != RequestUpdateResult::Rendered) std::_Exit(2);
  {
    RenderLock lock;
    if (fixture.batches != 1 || fixture.reads != 1 || fixture.timeReads != (fixture.page.textOnly ? 0u : 1u) ||
        fixture.commands) {
      LOG_ERR("BST", "Glyph widget paint/query isolation failed");
      std::_Exit(3);
    }
    std::filesystem::create_directories(output);
    static constexpr const char* names[] = {"known",       "empty",    "not-saved",   "off",
                                            "unsupported", "oversize", "unavailable", "stale"};
    const auto path =
        std::filesystem::path(output) / (std::string(fixture.page.textOnly ? "x3-off" : names[scene]) + ".pgm");
    FILE* image = std::fopen(path.c_str(), "wb");
    if (!image) std::_Exit(4);
    std::fprintf(image, "P5\n%d %d\n255\n", renderer.getScreenWidth(), renderer.getScreenHeight());
    for (int y = 0; y < renderer.getScreenHeight(); ++y)
      for (int x = 0; x < renderer.getScreenWidth(); ++x) std::fputc(renderer.isPixelBlack(x, y) ? 0 : 255, image);
    std::fclose(image);
  }
  frame = 0;
  if (++scene == 8 || fixture.page.textOnly) {
    LOG_INF("BST", "BookSync Glyph PASS: scenes=%u host=%u projection-alloc=0 paint-queries=0", scene,
            unsigned(sizeof(WidgetHost)));
    std::fflush(nullptr);
    std::_Exit(0);
  }
  return true;
}
}  // namespace glyphWidgetScenario
