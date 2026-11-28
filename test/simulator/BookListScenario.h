// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstring>

#include "AppCapabilities.h"
#include "ApprovedWidgetCovers.h"
#include "WidgetScenario.h"
#include "booksync/services/OfflineLibrary.h"
#include "booksync/services/widgets/BookList.h"
#include "components/UITheme.h"
namespace bookListScenario {
using namespace booksync;
constexpr WindowSource Sources[] = {WindowSource::AlsoReading,  WindowSource::Recent,     WindowSource::WantToRead,
                                    WindowSource::AllFinished,  WindowSource::PickBackUp, WindowSource::UpNext,
                                    WindowSource::FinishedYear, WindowSource::Unopened,   WindowSource::OnDevice};
constexpr const char* SourceNames[] = {"also-reading", "recent",        "want-to-read", "all-finished", "pick-back-up",
                                       "up-next",      "finished-year", "unopened",     "on-device"};
constexpr const char* StateNames[] = {"known",       "empty", "not-saved", "off",
                                      "unsupported", "error", "oversize",  "stale"};
class Fixture final : public Library {
 public:
  HomePage page{};
  LibraryWindow data{};
  BookListProjection preview{};
  unsigned scene = 0, batches = 0, queries = 0, browses = 0, opens = 0, retiredOpens = 0;
  uint64_t sequence = 0;
  mutable unsigned copies = 0, coverCopies = 0;
  unsigned coverIntents = 0;
  WindowSource selected = WindowSource::Unknown;
  Capabilities capabilities() const override { return {}; }
  Snapshot snapshot() const override {
    Snapshot out;
    out.accountEpoch = 3;
    return out;
  }
  HomePage homePage() const override { return page; }
  bool requestHomePage(uint8_t) override { return true; }
  Subscription subscribe() const override { return {data.publication, 1, true}; }
  void readWindow(LibraryWindow& out) const override {
    ++copies;
    out = data;
  }
  CommandStatus submit(Intent) override { return CommandStatus::Unavailable; }
  CommandStatus open(BookKey) override {
    ++opens;
    return CommandStatus::Accepted;
  }
  void retireOpen(BookKey) override { ++retiredOpens; }
  CommandStatus browseSource(WindowSource source) override {
    ++browses;
    selected = source;
    return offlineLibrary().browseSource(source);
  }
  CoverReference coverReference(BookKey key) const override { return {key.ordinal + 1, CoverWidth, 180}; }
  bool readCover(CoverReference reference, CoverBitmap& out) const override {
    ++coverCopies;
    const auto ordinal = reference.id - 1;
    const auto rank = ordinal % 100;
    const bool reading = ordinal / 100 == static_cast<uint8_t>(WindowSource::AlsoReading);
    return approvedWidgetCovers::copy(1 + (rank + (reading ? 1 : 0)) % 4, out);
  }
  CommandStatus requestCover(BookKey) override {
    ++coverIntents;
    return CommandStatus::Unavailable;
  }
  static uint32_t sourceTotal(WindowSource source) {
    constexpr uint32_t counts[] = {14, 6, 37, 18, 8, 5, 18, 12, 312};
    for (uint8_t i = 0; i < 9; ++i)
      if (Sources[i] == source) return counts[i];
    return 0;
  }
  static BookListMeta meta(void* context, WindowSource source) {
    const auto& fixture = *static_cast<Fixture*>(context);
    BookListMeta value;
    value.provenance = {4, 3, 17, 0};
    value.total = value.saved = fixture.scene == 1 ? 0 : sourceTotal(source);
    value.countKnown = value.complete = true;
    value.year = 2026;
    value.stale = fixture.scene == 7;
    value.ageKnown = value.stale;
    value.ageDays = value.stale ? 2 : 0;
    const PersonalState states[] = {PersonalState::Ready, PersonalState::Empty,       PersonalState::NotSaved,
                                    PersonalState::Off,   PersonalState::Unsupported, PersonalState::Unavailable,
                                    PersonalState::Ready, PersonalState::Ready};
    value.state = states[fixture.scene];
    if (fixture.scene == 6) value.bytes = LibraryBytes + 1;
    return value;
  }
  static bool book(void*, WindowSource source, uint32_t rank, BookRow& out) {
    out = {};
    out.key = {{4, 3, 17, 0}, static_cast<uint32_t>(source) * 100 + rank};
    constexpr const char* names[] = {"The Orchard Map", "Paper Horizons", "The Slow Road", "Blue Harbour"};
    const bool reading = source == WindowSource::AlsoReading;
    std::snprintf(out.title.data(), out.title.size(), "%s", names[(rank + (reading ? 1 : 0)) % 4]);
    out.readingKnown = true;
    constexpr uint32_t positions[] = {38, 81, 12};
    out.page = positions[rank % 3];
    out.pages = 100;
    return true;
  }
  bool requestBatch(WindowBatchRequest batch) override {
    ++batches;
    data = {};
    data.publication = ++sequence;
    data.windowCount = 1;
    data.provenance = {4, 3, 17, 0};
    projectBookList({this, meta, book,
                     [](void*, WindowSource, uint32_t rank) {
                       return rank % 2 ? FileAvailability::ServerOnly : FileAvailability::OnCard;
                     }},
                    batch.windows[0], {17, 3}, 2026, preview);
    auto& span = data.windows[0];
    span.request = batch.windows[0];
    span.total = preview.total;
    span.count = preview.count;
    span.bytes = sizeof(WindowSpan) + span.count * sizeof(BookRow);
    span.stale = preview.stale;
    span.ageKnown = preview.ageKnown;
    span.ageDays = preview.ageDays;
    span.availability = preview.state == PersonalState::NotSaved      ? Availability::NotSaved
                        : preview.state == PersonalState::Off         ? Availability::Off
                        : preview.state == PersonalState::Unsupported ? Availability::Unsupported
                        : preview.state == PersonalState::Unavailable ? Availability::Unavailable
                                                                      : Availability::Ready;
    if (preview.state == PersonalState::Oversized) span.bytes = span.request.bytes + 1;
    data.count = preview.count;
    for (uint16_t i = 0; i < preview.count; ++i) {
      data.rows[i] = preview.books[i];
      data.fileAvailability[i] = preview.files[i];
    }
    return true;
  }
  bool requestWindow(WindowRequest request) override {
    ++queries;
    data = {};
    data.publication = ++sequence;
    data.windowCount = 1;
    data.provenance = {4, 3, 17, 0};
    auto& span = data.windows[0];
    span.request = request;
    span.total = scene == 1 ? 0 : sourceTotal(request.source);
    span.count = span.total > request.offset ? std::min<uint32_t>(request.limit, span.total - request.offset) : 0;
    span.availability = Availability::Ready;
    span.bytes = sizeof(WindowSpan) + span.count * sizeof(BookRow);
    data.count = span.count;
    for (uint16_t i = 0; i < span.count; ++i) {
      book(this, request.source, request.offset + i, data.rows[i]);
      data.fileAvailability[i] = FileAvailability::OnCard;
    }
    return true;
  }
};
inline void capture(Fixture& fixture, const char* output, const std::string& name) {
  const auto reads = fixture.copies, covers = fixture.coverCopies, intents = fixture.coverIntents;
  if (activityManager.requestUpdateAndWait() != RequestUpdateResult::Rendered) std::_Exit(101);
  RenderLock lock;
  if (fixture.copies != reads || fixture.coverCopies != covers || fixture.coverIntents != intents) std::_Exit(102);
  std::filesystem::create_directories(output);
  auto path = std::filesystem::path(output) / (name + ".pgm");
  FILE* file = std::fopen(path.c_str(), "wb");
  if (!file) std::_Exit(103);
  std::fprintf(file, "P5\n%d %d\n255\n", renderer.getScreenWidth(), renderer.getScreenHeight());
  for (int y = 0; y < renderer.getScreenHeight(); ++y)
    for (int x = 0; x < renderer.getScreenWidth(); ++x) std::fputc(renderer.isPixelBlack(x, y) ? 0 : 255, file);
  std::fclose(file);
}
inline bool tick() {
  const char* output = std::getenv("BOOKSYNC_BOOKLIST_CAPTURES");
  if (!output) return false;
  mappedInputManager.simulatorClearInputFrame();
  static Fixture fixtures[2];
  static unsigned frame = 0, cycle = 0;
  const bool text = std::strcmp(Layout::deviceClass(), "x3") == 0;
  const bool cancelProbe = !text && std::getenv("BOOKSYNC_BOOKLIST_CANCEL_SCENE");
  const unsigned preset = text || cancelProbe ? 8 : cycle / 8, state = text || cancelProbe ? 0 : cycle % 8;
  auto& fixture = fixtures[cycle % 2];
  if (frame++ == 0) {
    approvedWidgetCovers::load(1, 4);
    fixture.page = {};
    fixture.page.revision = 1;
    fixture.page.count = 1;
    fixture.page.rows = text ? 7 : 6;
    fixture.page.textOnly = text;
    fixture.page.widgets[0] = {
        WidgetKind::Shelf, Sources[preset], 0, 0, 2, 2, static_cast<uint8_t>(preset == 0 || preset == 8 ? 1 : 0)};
    fixture.scene = state;
    fixture.batches = fixture.queries = fixture.browses = fixture.opens = fixture.copies = fixture.coverCopies =
        fixture.coverIntents = fixture.retiredOpens = 0;
    showHomeScene(renderer, mappedInputManager, fixture);
  }
  const std::string name = std::string(SourceNames[preset]) + "-" + StateNames[state];
  if (frame == 28) capture(fixture, output, name);
  const bool all = !text && (state == 0 || state == 1 || state == 7);
  if (frame == 30 && (all || state == 5 || state == 6 || text))
    mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Confirm);
  if (all && frame == 54) capture(fixture, output, name + "-all");
  if (all && frame == 56)
    mappedInputManager.simulatorInjectRelease(cancelProbe ? MappedInputManager::Button::Confirm
                                                          : MappedInputManager::Button::Back);
  if (cancelProbe) {
#if CROSSINK_APP_CAP_TOUCH
    if (mappedInputManager.hasTouch()) {
      const auto area = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
      if (frame == 58) mappedInputManager.simulatorInjectTouchDown(area.x + 40, area.y + 24);
      if (frame == 59) mappedInputManager.simulatorInjectTouchRelease(area.x + 40, area.y + 24);
    } else
#endif
    {
      if (frame == 58) mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Back);
    }
  }
  if (frame < 80) return true;
  const unsigned batches = all || state == 5 || state == 6 ? 2 : 1;
  const unsigned reads = all ? 3 : batches;
  if (fixture.batches != batches || fixture.copies != reads || fixture.browses != (all ? 1u : 0u) ||
      fixture.queries != (all ? 1u : 0u) || fixture.opens != (text || cancelProbe ? 1u : 0u) ||
      fixture.retiredOpens != (cancelProbe ? 1u : 0u) || (all && fixture.selected != Sources[preset])) {
    LOG_ERR("BSW", "Book list scene %u: batch=%u copy=%u browse=%u query=%u open=%u", cycle, fixture.batches,
            fixture.copies, fixture.browses, fixture.queries, fixture.opens);
    std::fflush(nullptr);
    std::_Exit(104);
  }
  if (text && (fixture.coverCopies || fixture.coverIntents)) std::_Exit(105);
  if (all) capture(fixture, output, name + "-back");
  frame = 0;
  if (++cycle == (text || cancelProbe ? 1u : 72u)) {
    LOG_INF("BSW", "BookSync Book list PASS: %u scenes, bounded covers, source All and Back, no paint queries", cycle);
    std::fflush(nullptr);
    std::_Exit(0);
  }
  return true;
}
}  // namespace bookListScenario
