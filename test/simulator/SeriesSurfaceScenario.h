// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "activities/Activity.h"
#include "booksync/system/Bootstrap.h"
#include "booksync/system/SeriesScreen.h"
#include "fontIds.h"
extern GfxRenderer renderer;
extern MappedInputManager mappedInputManager;
namespace seriesSurfaceScenario {
using namespace booksync;
inline constexpr const char* Names[] = {"series-ready",
                                        "series-offline",
                                        "series-incomplete",
                                        "series-saved",
                                        "series-unknown",
                                        "series-loading",
                                        "series-not-saved",
                                        "series-empty",
                                        "series-error",
                                        "series-preview",
                                        "series-offline-preview",
                                        "series-gap",
                                        "series-preview-ten",
                                        "series-count",
                                        "series-count-invalid",
                                        "series-files",
                                        "series-review",
                                        "series-connect-failed",
                                        "series-file-unavailable",
                                        "series-preview-error",
                                        "series-incomplete-notice",
                                        "series-unavailable",
                                        "series-no-eligible",
                                        "series-queue",
                                        "series-paused",
                                        "series-auth",
                                        "series-resume",
                                        "series-file-error",
                                        "series-row-canceled",
                                        "series-canceled",
                                        "series-complete"};
struct Fixture final : Library {
  uint8_t scene = 0;
  mutable uint32_t reads = 0;
  uint32_t writes = 0, opens = 0;
  uint64_t nextFlow = 0;
  QueryResult members{};
  SeriesPlanView plan{};
  SeriesFlowView value{};
  Capabilities capabilities() const override { return {true, true, true}; }
  Snapshot snapshot() const override {
    ++reads;
    return {Availability::Ready, 2, 3};
  }
  AccountView accountView() const override {
    ++reads;
    AccountView v;
    v.ticket = {7, 3};
    return v;
  }
  CommandStatus submit(Intent) override {
    ++writes;
    return CommandStatus::Unavailable;
  }
  CommandStatus open(BookKey) override {
    ++opens;
    ++writes;
    return CommandStatus::Accepted;
  }
  bool requestQuery(const QueryRequest& request) override {
    ++writes;
    members.clear();
    members.provenance = {2, 3, 7, 1};
    members.requestId = request.requestId;
    members.publication = request.requestId;
    members.count = 3;
    members.matchedBooks = 8;
    members.hasMore = true;
    members.availability = Availability::Ready;
    static constexpr const char* Titles[] = {"First Light", "River Glass", "Stone Path"};
    for (uint8_t i = 0; i < 3; ++i) {
      auto& row = members.entries[i];
      row.book.key = {members.provenance, i};
      std::strcpy(row.book.title.data(), Titles[i]);
      std::strcpy(row.label.data(), "Example Press");
      row.book.readingKnown=i==1;row.book.page=42;row.book.pages=100;
      row.seriesPositionKnown = true;
      row.seriesPosition = i == 1 ? 1.5 : i == 2 ? 2 : 1;
      row.onDevice = i < 2 ? QueryTruth::Yes : QueryTruth::No;
      row.status = i == 0 ? QueryShow::Finished : i == 1 ? QueryShow::Reading : QueryShow::Unread;
    }
    if (scene == 5 || scene == 6) {
      members.count = 0;
      members.availability = Availability::NotSaved;
    }
    if (scene == 7) {
      members.count = 0;
      members.matchedBooks = 0;
    }
    if (scene == 8) {
      members.count = 0;
      members.availability = Availability::Unavailable;
    }
    return true;
  }
  void readQuery(QueryResult& out) const override {
    ++reads;
    out = members;
  }
  bool requestSeries(SeriesRequest request) override {
    ++writes;
    plan.clear();
    plan.requestId = request.requestId;
    plan.provenance = {2, 3, 7, 1};
    plan.total = 8;
    plan.countsKnown = true;
    plan.offline = scene == 1;
    plan.read = 2;
    plan.reading = 1;
    plan.count = 3;
    plan.deviceKnown = true;
    plan.availability = Availability::Ready;
    std::strcpy(plan.title.data(), "The Lantern Cycle");
    std::strcpy(plan.author.data(), "Example Press");
    for (uint8_t i = 0; i < 3; ++i) {
      plan.next[i].book.key = {plan.provenance, uint32_t(i + 2)};
      plan.next[i].positionKnown=true;plan.next[i].position=i+2;
      std::snprintf(plan.next[i].book.title.data(), 96, "%s",
                    i == 0   ? "Stone Path"
                    : i == 1 ? "Far Shore"
                             : "Winter Gate");
    }
    if (scene == 2) {
      plan.membershipIncomplete = true;
      plan.availability = Availability::Unsupported;
    }
    if (scene == 3) {
      plan.remainingSaved = true;
      plan.count = 0;
    }
    if (scene == 4) {
      plan.deviceKnown = false;
      plan.count = 0;
    }
    if (scene == 5) {
      plan.pending = true;
      plan.availability = Availability::NotSaved;
    }
    if (scene == 6) plan.availability = Availability::NotSaved;
    return true;
  }
  void readSeries(SeriesPlanView& out) const override {
    ++reads;
    out = plan;
  }
  CommandStatus requestCover(BookKey) override {
    ++writes;
    return CommandStatus::Accepted;
  }
  CoverReference coverReference(BookKey key) const override {
    ++reads;
    return {key.ordinal + 1, CoverWidth, CoverHeight};
  }
  bool readCover(CoverReference reference, CoverBitmap& out) const override {
    ++reads;
    out.pixels.fill(0xff);
    out.width = CoverWidth;
    out.height = CoverHeight;
    // Original monochrome geometric jackets; synthetic, never live cover data.
    for (unsigned y = 0; y < CoverHeight; ++y)
      for (unsigned x = 0; x < CoverWidth; ++x)
        if (x < 4 || x > 123 || y < 4 || y > 187 || (y > 70 && y < 105 && ((x + y + reference.id * 11) % 37) < 10) ||
            (y > 135 && y < 145))
          out.pixels[y * 16 + x / 8] &= ~(0x80 >> (x % 8));
    return true;
  }
  CommandStatus seriesAction(const SeriesFlowRequest& request) override {
    ++writes;
    if (request.action == SeriesAction::Close) return CommandStatus::Accepted;
    if (request.action != SeriesAction::Begin) {
      ++value.revision;
      return CommandStatus::Accepted;
    }
    value.clear();
    value.flow = ++nextFlow;
    value.owner = request.owner;
    value.requestId = request.series.requestId;
    value.revision = 1;
    value.online = scene != 10 && scene != 26;
    value.plan = plan;
    value.plan.token = 42;
    value.plan.requested = scene == 12 ? 10 : 3;
    value.plan.count = scene == 12 ? 10 : 3;
    value.plan.earlierKnown = scene == 11;
    std::strcpy(value.plan.earlier.book.title.data(), "River Glass");
    value.plan.earlier.book.key = {plan.provenance, 1};
    for (uint8_t i = 0; i < value.plan.count; ++i) {
      auto& member = value.plan.next[i];
      member.book.key = {plan.provenance, uint32_t(i + 2)};
      std::snprintf(member.book.title.data(), 96, "%s",
                    i % 3 == 0   ? "Stone Path"
                    : i % 3 == 1 ? "Far Shore"
                                 : "Winter Gate");
      member.positionKnown = true;
      member.position = i + 2;
      value.review[i].bytes = (i + 1) * 1048576;
      value.review[i].sizeKnown = i != 1;
      value.review[i].supported = true;
      std::strcpy(value.review[i].label.data(), "EPUB");
      std::strcpy(value.review[i].destination.data(), "Books folder");
    }
    value.scene = SeriesFlowScene::Preview;
    if (scene == 15) {
      value.scene = SeriesFlowScene::Files;
      value.fileCount = 2;
      value.fileRow = 0;
      std::strcpy(value.files[0].label.data(), "EPUB · original");
      std::strcpy(value.files[1].label.data(), "EPUB · revised");
      value.files[0].choice = 1;
      value.files[1].choice = 2;
      value.files[0].sizeKnown = true;
      value.files[0].bytes = 1048576;
    }
    if (scene == 16) {
      value.scene = SeriesFlowScene::Review;
      value.review[2].supported = false;
    }
    if (scene >= 17 && scene <= 22) {
      value.scene = SeriesFlowScene::Notice;
      value.notice = static_cast<SeriesNotice>(scene - 17);
    }
    if (scene >= 23) {
      value.scene = SeriesFlowScene::Queue;
      value.queue.id = 91;
      value.queue.count = 3;
      value.queue.sequence = 1;
      value.queue.availability = Availability::Ready;
      value.queue.paused = scene >= 24 && scene <= 27;
      value.queue.complete = scene >= 29;
      for (uint8_t i = 0; i < 3; ++i) {
        auto& row = value.queue.items[i];
        row.title = value.plan.next[i].book.title;
        row.job = i + 1;
        row.state = i == 0 ? SeriesItemState::Complete : i == 1 ? SeriesItemState::Running : SeriesItemState::Queued;
      }
      if (scene >= 24 && scene <= 26) value.queue.items[1].state = SeriesItemState::Paused;
      if (scene == 27) value.queue.items[1].state = SeriesItemState::Failed;
      if (scene == 28 || scene == 29) value.queue.items[1].state = SeriesItemState::Canceled;
      if (scene == 29) value.queue.items[2].state = SeriesItemState::Canceled;
      if (scene == 30)
        for (auto& row : value.queue.items) row.state = SeriesItemState::Complete;
    }
    return CommandStatus::Accepted;
  }
  void readSeriesFlow(SeriesFlowView& out) const override {
    ++reads;
    out = value;
  }
};
inline void rawCapture(const char* directory, const char* name, int width, int height) {
  std::filesystem::create_directories(directory);
  auto path = std::filesystem::path(directory) / (std::string(name) + ".pgm");
  FILE* file = std::fopen(path.c_str(), "wb");
  if (!file) std::_Exit(31);
  std::fprintf(file, "P5\n%d %d\n255\n", width, height);
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) std::fputc(renderer.isPixelBlack(x, y) ? 0 : 255, file);
  if (std::fclose(file)) std::_Exit(32);
}
inline bool tick() {
  const char* directory = std::getenv("BOOKSYNC_SERIES_SURFACES");
  if (!directory) return false;
  mappedInputManager.simulatorClearInputFrame();
  static Fixture fixture;
  static unsigned frame = 0;
  const auto f = frame++;
  if (f == 0) showHomeScene(renderer, mappedInputManager, fixture);
  if (f >= 8 && f < 8 + 31 * 24) {
    const unsigned scene = (f - 8) / 24, step = (f - 8) % 24;
    if (step == 0) {
      fixture.scene = scene;
      showSeries(renderer, mappedInputManager, fixture, "synthetic-series");
    }
    if (step == 4 && scene >= 9) seriesScenarioBegin(scene == 13 || scene == 14, scene == 14 ? "11" : nullptr);
    if (step == 12) {
      const auto reads = fixture.reads, writes = fixture.writes;
      if (activityManager.requestUpdateAndWait() != RequestUpdateResult::Rendered) std::_Exit(33);
      RenderLock lock;
      if (reads != fixture.reads || writes != fixture.writes) std::_Exit(34);
      if(!seriesScenarioInvariant())std::_Exit(36);
      rawCapture(directory, Names[scene], renderer.getScreenWidth(), renderer.getScreenHeight());
    }
    if (step == 16 || (step == 20 && scene >= 9) || (step == 22 && scene == 16))
      mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Back);
  }
  if (f == 8 + 31 * 24 + 8) {
    RenderLock lock;
    renderer.clearScreen();
    drawSeriesGroupHeader(renderer, 8, 8, renderer.getScreenWidth() - 16, "The Lantern Cycle", 8);
    rawCapture(directory, "series-group-header", renderer.getScreenWidth(), 64);
    if (fixture.opens) std::_Exit(35);
    LOG_INF("BSS",
            "BookSync F36c surfaces PASS: 31 approved Series captures; top-right All header; paint reads=0; implicit "
            "opens=0");
    std::fflush(nullptr);
    std::_Exit(0);
  }
  return true;
}
}  // namespace seriesSurfaceScenario
