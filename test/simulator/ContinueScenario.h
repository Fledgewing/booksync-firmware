// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstring>

#include "ApprovedWidgetCovers.h"
#include "WidgetScenario.h"
#include "booksync/services/widgets/Continue.h"
namespace continueScenario {
using namespace booksync;
class Fixture final : public Library {
 public:
  HomePage page{};
  LibraryWindow data{};
  unsigned scene = 0, batches = 0, opens = 0;
  uint64_t sequence = 0;
  mutable unsigned reads = 0, coverReads = 0;
  unsigned coverRequests = 0;
  Capabilities capabilities() const override { return {}; }
  Snapshot snapshot() const override { return {}; }
  HomePage homePage() const override { return page; }
  bool requestHomePage(uint8_t) override { return true; }
  Subscription subscribe() const override { return {data.publication, 1, true}; }
  void readWindow(LibraryWindow& out) const override {
    ++reads;
    out = data;
  }
  CommandStatus submit(Intent intent) override {
    if (intent != Intent::OpenLastBook) return CommandStatus::Unavailable;
    ++opens;
    return CommandStatus::Accepted;
  }
  CoverReference coverReference(BookKey) const override {
    return (scene == 0 || scene == 8) ? CoverReference{1, CoverWidth, CoverHeight} : CoverReference{};
  }
  bool readCover(CoverReference, CoverBitmap& out) const override {
    ++coverReads;
    return approvedWidgetCovers::copy(0, out);
  }
  CommandStatus requestCover(BookKey) override {
    ++coverRequests;
    return CommandStatus::Unavailable;
  }
  bool requestBatch(WindowBatchRequest batch) override {
    ++batches;
    data = {};
    data.publication = ++sequence;
    data.windowCount = 1;
    data.count = scene == 1 ? 0 : 1;
    auto& span = data.windows[0];
    span.request = batch.windows[0];
    span.availability = Availability::Ready;
    span.count = data.count;
    span.total = data.count;
    span.bytes = sizeof(WindowSpan) + span.count * sizeof(BookRow) + sizeof(ContinueValues);
    auto& book = data.rows[0];
    std::snprintf(book.title.data(), book.title.size(), "%s",
                  scene == 8 ? "An Orchard Map for the Far Coast" : "The Orchard Map");
    book.page = 36;
    book.pages = 100;
    book.readingKnown = true;
    PersonalCard card;
    card.timeLeft = {7200, true};
    card.finishDay = {3, true};
    const PersonalState states[] = {PersonalState::Ready,     PersonalState::Empty,       PersonalState::NotSaved,
                                    PersonalState::Off,       PersonalState::Unsupported, PersonalState::Unavailable,
                                    PersonalState::Oversized, PersonalState::Ready,       PersonalState::Ready};
    ContinueContext context;
    std::snprintf(context.seriesLabel.data(), context.seriesLabel.size(), "River paths · Book 2");
    std::snprintf(context.chapterLabel.data(), context.chapterLabel.size(), "Chapter 3");
    projectContinue(scene != 1, states[scene], card, scene == 7, 2, data.resume, &context, true, &book);
    return true;
  }
};
inline bool tick() {
  const char* output = std::getenv("BOOKSYNC_CONTINUE_CAPTURES");
  if (!output) return false;
  mappedInputManager.simulatorClearInputFrame();
  static Fixture fixtures[2];  // Old activity retains an immutable source until retirement.
  static unsigned frame = 0, scene = 0;
  auto& fixture = fixtures[scene % 2];
  if (frame++ == 0) {
    approvedWidgetCovers::load(0, 1);
    fixture.page = {};
    fixture.page.revision = 1;
    fixture.page.count = 1;
    fixture.page.rows = 6;
    fixture.page.widgets[0] = {WidgetKind::Continue, WindowSource::Continue, 0, 0, 2, 2, 0};
    fixture.page.textOnly = std::strcmp(Layout::deviceClass(), "x3") == 0;
    fixture.scene = scene;
    fixture.batches = fixture.reads = fixture.opens = fixture.coverReads = fixture.coverRequests = 0;
    showHomeScene(renderer, mappedInputManager, fixture);
  }
  if (frame == 20) mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Confirm);
  if (frame < 40) return true;
  if (activityManager.requestUpdateAndWait() != RequestUpdateResult::Rendered) std::_Exit(2);
  {
    RenderLock lock;
    const unsigned expectedBatches = scene == 5 || scene == 6 ? 2u : 1u;
    if (fixture.batches != expectedBatches || fixture.reads != expectedBatches ||
        fixture.opens != (scene == 0 || scene == 7 || scene == 8 ? 1u : 0u)) {
      LOG_ERR("BSW", "Continue scene %u: batches=%u reads=%u opens=%u", scene, fixture.batches, fixture.reads,
              fixture.opens);
      std::_Exit(3);
    }
    if (fixture.coverReads != (!fixture.page.textOnly && (scene == 0 || scene == 8) ? 1u : 0u) ||
        fixture.coverRequests != (!fixture.page.textOnly && scene == 7 ? 1u : 0u))
      std::_Exit(6);
    std::filesystem::create_directories(output);
    const char* names[] = {"known", "empty",    "not-saved", "off",       "unsupported",
                           "error", "oversize", "stale",     "long-title"};
    const auto file = std::filesystem::path(output) / (std::string(names[scene]) + ".pgm");
    FILE* image = std::fopen(file.c_str(), "wb");
    if (!image) std::_Exit(4);
    std::fprintf(image, "P5\n%d %d\n255\n", renderer.getScreenWidth(), renderer.getScreenHeight());
    for (int y = 0; y < renderer.getScreenHeight(); ++y)
      for (int x = 0; x < renderer.getScreenWidth(); ++x) std::fputc(renderer.isPixelBlack(x, y) ? 0 : 255, image);
    std::fclose(image);
  }
  frame = 0;
  if (++scene == 9) {
    LOG_INF(
        "BSW",
        "BookSync Continue PASS: nine scenes, eight states, one initial batch/read, explicit retry, guarded Resume");
    std::fflush(nullptr);
    std::_Exit(0);
  }
  return true;
}
}  // namespace continueScenario
