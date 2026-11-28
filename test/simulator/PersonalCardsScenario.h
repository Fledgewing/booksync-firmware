// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AppCapabilities.h"
#include "ApprovedWidgetCovers.h"
#include "BookListScenario.h"
#include "WidgetScenario.h"
#include "booksync/services/widgets/PersonalWidgets.h"
#include "booksync/ui/CompactGrid.h"
namespace personalCardsScenario {
using namespace booksync;
constexpr const char* States[] = {"known", "empty",    "not-saved", "off",     "unsupported",
                                  "error", "oversize", "stale",     "another", "recommended-open"};
struct Source final : PersonalReadingSource {
  PersonalState state = PersonalState::Ready;
  ReadingScope owner() const override { return {}; }
  PersonalState availability(PersonalKind) const override { return state; }
  static const char* name(uint32_t n) {
    constexpr const char* names[] = {"Northbound", "The Quiet Lake", "Winter Signal", "Open Water", "Saved book"};
    return n < 4 ? names[n] : names[4];
  }
  PersonalStep read(PersonalKind, uint32_t n, PersonalRecord& out) override {
    if (n >= 37) return PersonalStep::End;
    out = {};
    // Frozen normalized identities exercise the real daily ranker. Their
    // ranked first four rows reproduce the approved gallery example.
    constexpr uint8_t identities[] = {17, 30, 7,  1,  5,  35, 13, 16, 20, 22, 18, 4,  12, 3,  6,  14, 8,  9, 32,
                                      23, 10, 36, 28, 33, 29, 24, 2,  19, 11, 31, 25, 21, 27, 15, 0,  26, 34};
    std::snprintf(out.identity.data(), out.identity.size(), "approved-%02u", identities[n]);
    std::strcpy(out.book.title.data(), name(n));
    out.book.key = {{9, 1, 0, 3}, n};
    return PersonalStep::Record;
  }
  PersonalStep find(PersonalKind, std::string_view identity, uint32_t& ordinal) override {
    PersonalRecord candidate;
    for (uint32_t n = 0; n < 37; ++n) {
      read(PersonalKind::Discover, n, candidate);
      if (identity == candidate.identity.data()) {
        ordinal = n;
        return PersonalStep::Record;
      }
    }
    return PersonalStep::End;
  }
};
class Fixture final : public Library {
 public:
  Source source;
  PersonalReadingSource* sources[1] = {&source};
  PersonalReading personal{sources, 1};
  PersonalWidgetValues discover{}, recommended{};
  DiscoverProjection projection{personal, discover};
  PersonalWidgetCapture capture{{9, 1, 0, 3}, {}, false, false, 0};
  HomePage page{};
  LibraryWindow data{};
  WindowBatchRequest savedBatch{};
  unsigned scene = 0, batches = 0, browses = 0, shuffles = 0, opens = 0, queries = 0, coverIntents = 0;
  uint64_t sequence = 0;
  mutable unsigned copies = 0, coverCopies = 0;
  Capabilities capabilities() const override { return {}; }
  Snapshot snapshot() const override {
    Snapshot value;
    value.accountEpoch = 1;
    return value;
  }
  HomePage homePage() const override { return page; }
  bool requestHomePage(uint8_t) override { return true; }
  Subscription subscribe() const override { return {data.publication, 1, true}; }
  void readWindow(LibraryWindow& out) const override {
    ++copies;
    out = data;
  }
  CommandStatus submit(Intent) override { return CommandStatus::Unavailable; }
  CommandStatus open(BookKey book) override {
    ++opens;
    return book.provenance.owner ? CommandStatus::Unavailable : CommandStatus::Accepted;
  }
  CommandStatus browseSource(WindowSource chosen) override {
    if (chosen != WindowSource::Random) std::_Exit(121);
    ++browses;
    return offlineLibrary().browseSource(chosen);
  }
  CommandStatus anotherDiscover() override {
    ++shuffles;
    requestBatch(savedBatch);
    return CommandStatus::Accepted;
  }
  CoverReference coverReference(BookKey book) const override { return {book.ordinal + 1, CoverWidth, 180}; }
  bool readCover(CoverReference ref, CoverBitmap& out) const override {
    ++coverCopies;
    return approvedWidgetCovers::copy(5 + (ref.id - 1) % 4, out);
  }
  CommandStatus requestCover(BookKey) override {
    ++coverIntents;
    return CommandStatus::Unavailable;
  }
  static Availability available(PersonalWidgetState state) {
    return state == PersonalWidgetState::Ready || state == PersonalWidgetState::Empty ? Availability::Ready
           : state == PersonalWidgetState::Off                                        ? Availability::Off
           : state == PersonalWidgetState::Unsupported                                ? Availability::Unsupported
           : state == PersonalWidgetState::NotSaved                                   ? Availability::NotSaved
                                                                                      : Availability::Unavailable;
  }
  bool requestBatch(WindowBatchRequest batch) override {
    ++batches;
    savedBatch = batch;
    data.count = 0;
    data.windowCount = batch.count;
    data.publication = ++sequence;
    data.resume = {};
    for (auto& span : data.windows) span = {};
    for (auto& book : data.rows) book = {};
    constexpr PersonalState states[] = {PersonalState::Ready, PersonalState::Empty,       PersonalState::NotSaved,
                                        PersonalState::Off,   PersonalState::Unsupported, PersonalState::Unavailable,
                                        PersonalState::Ready, PersonalState::Ready,       PersonalState::Ready,
                                        PersonalState::Ready};
    source.state = states[scene];
    capture.stale = capture.ageKnown = scene == 7;
    capture.ageDays = capture.stale ? 2 : 0;
    if (!page.textOnly) {
      projection.begin(200, shuffles, capture, {static_cast<uint16_t>(scene == 6 ? 3 : 4), LibraryBytes});
      for (unsigned i = 0; projection.busy() && i < 10000; ++i) projection.tick(capture);
      if (projection.busy()) std::_Exit(122);
      personal.begin(PersonalKind::Recommended, 200, 0, {});
      for (unsigned i = 0; personal.busy() && i < 10000; ++i) personal.tick();
      if (personal.busy()) std::_Exit(123);
      projectRecommendedWidget(personal.result(), capture, {static_cast<uint16_t>(scene == 6 ? 0 : 1), LibraryBytes},
                               recommended);
    } else {
      discover.clear();
      recommended.clear();
    }
    for (uint8_t i = 0; i < batch.count; ++i) {
      auto& span = data.windows[i];
      span.request = batch.windows[i];
      span.first = data.count;
      span.bytes = sizeof(WindowSpan);
      if (span.request.source == WindowSource::Continue) {
        auto& book = data.rows[data.count++];
        book.key = {{1, 1, 0, 1}, 0};
        std::strcpy(book.title.data(), "Sibling reading");
        span.count = span.total = 1;
        span.availability = Availability::Ready;
        span.bytes += sizeof(BookRow);
        continue;
      }
      const auto& prepared = span.request.source == WindowSource::Random ? discover : recommended;
      span.count = prepared.count;
      span.total = prepared.total;
      span.stale = prepared.stale;
      span.ageKnown = prepared.ageKnown;
      span.ageDays = prepared.ageDays;
      span.availability = available(prepared.state);
      span.bytes += span.count * sizeof(BookRow);
      if (prepared.state == PersonalWidgetState::Oversize) span.bytes = span.request.bytes + 1;
      for (uint8_t n = 0; n < prepared.count; ++n) data.rows[data.count++] = prepared.books[n];
    }
    return true;
  }
  bool requestWindow(WindowRequest request) override {
    ++queries;
    data.provenance = {9, 1, 0, 3};
    data.count = 0;
    data.windowCount = 1;
    data.publication = ++sequence;
    auto& span = data.windows[0];
    span = {};
    span.request = request;
    span.total = scene == 1 ? 0 : 37;
    span.availability = Availability::Ready;
    for (uint32_t rank = request.offset; rank < span.total && span.count < request.limit; ++rank) {
      auto& book = data.rows[data.count++];
      book = {};
      book.key = {{9, 1, 0, 3}, rank};
      std::strcpy(book.title.data(), Source::name(rank));
      data.fileAvailability[data.count - 1] = FileAvailability::OnCard;
      ++span.count;
    }
    span.bytes = sizeof(WindowSpan) + span.count * sizeof(BookRow);
    return true;
  }
};
inline void save(Fixture& fixture, const char* output, const std::string& name) {
  const auto before = fixture.copies, coverReads = fixture.coverCopies, requests = fixture.coverIntents;
  if (activityManager.requestUpdateAndWait() != RequestUpdateResult::Rendered) std::_Exit(124);
  RenderLock lock;
  if (before != fixture.copies || coverReads != fixture.coverCopies || requests != fixture.coverIntents)
    std::_Exit(125);
  std::filesystem::create_directories(output);
  auto path = std::filesystem::path(output) / (name + ".pgm");
  FILE* file = std::fopen(path.c_str(), "wb");
  if (!file) std::_Exit(126);
  std::fprintf(file, "P5\n%d %d\n255\n", renderer.getScreenWidth(), renderer.getScreenHeight());
  for (int y = 0; y < renderer.getScreenHeight(); ++y)
    for (int x = 0; x < renderer.getScreenWidth(); ++x) std::fputc(renderer.isPixelBlack(x, y) ? 0 : 255, file);
  std::fclose(file);
}
inline bool tick() {
  const char* output = std::getenv("BOOKSYNC_PERSONAL_CARDS_CAPTURES");
  if (!output) return false;
  mappedInputManager.simulatorClearInputFrame();
  static Fixture fixtures[2];
  static unsigned frame = 0, cycle = 0;
  static BookKey initial{};
  const bool text = std::strcmp(Layout::deviceClass(), "x3") == 0;
  auto& fixture = fixtures[cycle % 2];
  if (frame++ == 0) {
    if (!text) approvedWidgetCovers::load(5, 4);
    fixture.page = {};
    fixture.page.revision = 1;
    fixture.page.rows = text ? 7 : 6;
    fixture.page.textOnly = text;
    fixture.page.count = 3;
    fixture.page.widgets[0] = {WidgetKind::Discover, WindowSource::Random, 0, 0, 2, 2};
    fixture.page.widgets[1] = {WidgetKind::Recommended, WindowSource::Recommended, 0, 2, 1, 1};
    fixture.page.widgets[2] = {WidgetKind::Continue, WindowSource::Continue, 0, 3, 2, 2};
    fixture.scene = cycle;
    fixture.batches = fixture.browses = fixture.shuffles = fixture.opens = fixture.queries = fixture.copies =
        fixture.coverCopies = fixture.coverIntents = 0;
    showHomeScene(renderer, mappedInputManager, fixture);
  }
  const std::string name = text ? "x3-excluded" : States[cycle];
  const bool all = !text && (cycle == 0 || cycle == 1 || cycle == 7);
  if (frame == 28) {
    save(fixture, output, name);
    if (!text) initial = fixture.discover.books[0].key;
  }
  if (!text) {
    bool touch = false;
#if CROSSINK_APP_CAP_TOUCH
    touch = mappedInputManager.hasTouch();
    if (touch && ((frame == 30 || frame == 31) && (all || cycle == 5 || cycle == 6 || cycle == 8) ||
                  (cycle == 9 && (frame == 44 || frame == 45)))) {
      int topInset, rightInset, bottomInset, leftInset;
      renderer.getOrientedViewableTRBL(&topInset, &rightInset, &bottomInset, &leftInset);
      const uint8_t widget = cycle == 9 ? 1 : 0;
      const auto box =
          CompactGrid::forHomePanel(renderer.getScreenWidth(), renderer.getScreenHeight(), topInset, rightInset,
                                    bottomInset, leftInset, fixture.page.cols, fixture.page.rows)
              .compactTile(fixture.page.widgets[widget]);
      const int x = box.x + (cycle == 9   ? 100
                             : cycle == 8 ? box.width - 116
                                          : box.width - 46),
                y = box.y + (cycle == 9 ? 64 : 25);
      if (frame == 30 || frame == 44)
        mappedInputManager.simulatorInjectTouchDown(x, y);
      else
        mappedInputManager.simulatorInjectTouchRelease(x, y);
    }
#endif
    if (!touch && (all || cycle == 5 || cycle == 6 || cycle == 8) && frame == 30) {
      if (cycle == 8)
        mappedInputManager.simulatorInjectRelease(mappedInputManager.menuButton(MappedInputManager::Button::Down));
      else
        mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Confirm);
    }
    if (!touch && cycle == 8 && frame == 34)
      mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Confirm);
    if (all && frame == 54) save(fixture, output, name + "-all");
    if (all && frame == 56) mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Back);
    if (cycle == 8 && frame == 54) {
      if (fixture.discover.books[0].key == initial) std::_Exit(127);
      save(fixture, output, "another-changed");
    }
    if (!touch && cycle < 8) {
      // The approval sheet focuses each isolated Classic card. Capture the
      // sibling through real Down events after the existing action/Back checks.
      const unsigned steps = cycle == 0 || cycle == 7 ? 6 : 1;
      for (unsigned n = 0; n < steps; ++n)
        if (frame == 64 + n * 2)
          mappedInputManager.simulatorInjectRelease(mappedInputManager.menuButton(MappedInputManager::Button::Down));
      if (frame == 78) save(fixture, output, name + "-recommended-focused");
    }
    if (!touch && cycle == 9) {
      for (unsigned n = 0; n < 6; ++n)
        if (frame == 30 + n * 2)
          mappedInputManager.simulatorInjectRelease(mappedInputManager.menuButton(MappedInputManager::Button::Down));
      if (frame == 44) mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Confirm);
    }
  }
  if (frame < 80) return true;
  if (fixture.shuffles != (cycle == 8 ? 1u : 0u) || fixture.browses != (all ? 1u : 0u) ||
      fixture.opens != (cycle == 9 ? 1u : 0u) || fixture.queries != (all ? 1u : 0u))
    std::_Exit(128);
  if (text && (fixture.coverCopies || fixture.coverIntents || fixture.data.windowCount != 1 ||
               fixture.savedBatch.windows[0].source != WindowSource::Continue))
    std::_Exit(129);
  if (all) save(fixture, output, name + "-back");
  frame = 0;
  if (++cycle == (text ? 1u : 10u)) {
    LOG_INF("BSPC",
            "BookSync personal cards PASS: %u scenes, real F39 projections, Another, All, Back and X3 exclusion",
            cycle);
    std::fflush(nullptr);
    std::_Exit(0);
  }
  return true;
}
}  // namespace personalCardsScenario
