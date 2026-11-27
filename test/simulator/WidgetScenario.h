// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "activities/Activity.h"
#include "booksync/services/Layout.h"
#include "booksync/system/Bootstrap.h"
#include "booksync/ui/WidgetHost.h"
extern GfxRenderer renderer;
extern MappedInputManager mappedInputManager;
namespace widgetScenario {
using namespace booksync;
class Fixture final : public Library {
 public:
  HomePage page{};
  LibraryWindow published{};
  unsigned batches = 0;
  mutable unsigned reads = 0;
  unsigned scene = 0;
  uint32_t opened = 0;
  Capabilities capabilities() const override { return {}; }
  Snapshot snapshot() const override { return {}; }
  HomePage homePage() const override { return page; }
  bool requestHomePage(uint8_t) override { return true; }
  Subscription subscribe() const override { return {published.publication, 1, true}; }
  void readWindow(LibraryWindow& out) const override {
    ++reads;
    out = published;
  }
  CommandStatus open(BookKey key) override {
    opened = key.ordinal;
    return CommandStatus::Accepted;
  }
  CommandStatus submit(Intent) override { return CommandStatus::Unavailable; }
  bool requestBatch(WindowBatchRequest batch) override {
    ++batches;
    ++published.publication;
    published.count = 0;
    published.windowCount = batch.count;
    for (unsigned i = 0; i < batch.count; ++i) {
      auto& span = published.windows[i];
      span = {};
      span.request = batch.windows[i];
      span.first = published.count;
      span.availability = scene == 3 ? Availability::NotSaved : Availability::Ready;
      span.bytes = sizeof(WindowSpan);
      span.total = scene == 1 ? 0 : 7;
      if (scene != 1 && scene != 3) {
        span.count = batch.windows[i].limit;
        span.bytes += span.count * sizeof(BookRow);
        for (uint16_t n = 0; n < span.count; ++n) {
          auto& row = published.rows[published.count++];
          std::snprintf(row.title.data(), row.title.size(), "Synthetic book %u", published.count);
          row.key.ordinal = published.count;
          row.readingKnown = true;
          row.page = 42;
          row.pages = 100;
        }
      }
      if (scene == 2 && i == 0) span.availability = Availability::Unavailable;
      if (scene == 2 && i == 1) span.bytes = 16385;
    }
    return true;
  }
};
inline bool tick() {
  const char* output = std::getenv("BOOKSYNC_WIDGET_CAPTURES");
  if (!output) return false;
  mappedInputManager.simulatorClearInputFrame();
  static Fixture fixture;
  static unsigned frame = 0, scene = 0;
  if (frame++ == 0) {
    static Layout layout;
    if (!layout.project(Layout::defaults(), Layout::deviceClass(), 0, fixture.page)) std::_Exit(1);
    fixture.scene = scene;
    fixture.batches = fixture.reads = 0;
    showHomeScene(renderer, mappedInputManager, fixture);
  }
  if (scene == 0 && (frame == 15 || frame == 18))
    mappedInputManager.simulatorInjectRelease(mappedInputManager.menuButton(MappedInputManager::Button::Down));
  if (scene == 0 && frame == 21) mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Confirm);
  if (frame < 40) return true;
  if (scene == 0 && fixture.opened != 3) {
    LOG_ERR("BSW", "Shelf focus selected %u instead of second book", unsigned(fixture.opened));
    std::_Exit(5);
  }
  if (activityManager.requestUpdateAndWait() != RequestUpdateResult::Rendered) std::_Exit(2);
  {
    RenderLock lock;
    if (fixture.batches != 1 || fixture.reads != 1) {
      LOG_ERR("BSW", "Home did repeated batch work: %u/%u", fixture.batches, fixture.reads);
      std::_Exit(3);
    }
    std::filesystem::create_directories(output);
    const char* names[] = {"known", "empty", "independent-errors", "not-saved"};
    const auto file = std::filesystem::path(output) / (std::string(names[scene]) + ".pgm");
    FILE* image = std::fopen(file.c_str(), "wb");
    if (!image) std::_Exit(4);
    std::fprintf(image, "P5\n%d %d\n255\n", renderer.getScreenWidth(), renderer.getScreenHeight());
    for (int y = 0; y < renderer.getScreenHeight(); ++y)
      for (int x = 0; x < renderer.getScreenWidth(); ++x) std::fputc(renderer.isPixelBlack(x, y) ? 0 : 255, image);
    std::fclose(image);
  }
  frame = 0;
  if (++scene == 4) {
    LOG_INF("BSW", "BookSync F34 PASS: four offline starter scenes, one batch/read, no paint queries; host=%u",
            unsigned(sizeof(WidgetHost)));
    std::fflush(nullptr);
    std::_Exit(0);
  }
  return true;
}
}  // namespace widgetScenario
