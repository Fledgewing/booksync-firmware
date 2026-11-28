// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstring>

#include "AppCapabilities.h"
#include "WidgetScenario.h"
#include "booksync/services/widgets/Collections.h"
#include "booksync/services/widgets/Continue.h"
#include "booksync/ui/CompactGrid.h"
#include "booksync/ui/widgets/Library.h"
#include "booksync/ui/widgets/Lists.h"
#include "components/UITheme.h"
#include "fontIds.h"
namespace collectionCardsScenario {
using namespace booksync;
inline constexpr const char* StateNames[] = {"known",       "empty", "not-saved", "off",
                                             "unsupported", "error", "oversize",  "stale"};
inline constexpr PersonalState States[] = {PersonalState::Ready, PersonalState::Empty,       PersonalState::NotSaved,
                                           PersonalState::Off,   PersonalState::Unsupported, PersonalState::Unavailable,
                                           PersonalState::Ready, PersonalState::Ready};
struct Fixture final : Library {
  HomePage page{};
  LibraryWindow data{};
  QueryRequest query{};
  QueryResult queryData{};
  SavedListsPage listsPage{};
  ListsWidgetMeta listMeta{};
  LibraryWidgetInput summary{};
  CatalogRoute route{};
  unsigned scene = 0, batches = 0, copies = 0, queryRequests = 0, listRequests = 0, queryCopies = 0, listCopies = 0,
           listRows = 0, resumes = 0, covers = 0, coverIntents = 0, newWidgetRequests = 0;
  uint64_t sequence = 0;
  inline static uint64_t nextSequence = 0;
  Capabilities capabilities() const override { return {}; }
  Snapshot snapshot() const override {
    Snapshot value;
    value.catalog = Availability::Ready;
    value.generation = 4;
    value.accountEpoch = 3;
    return value;
  }
  HomePage homePage() const override { return page; }
  bool requestHomePage(uint8_t) override { return true; }
  Subscription subscribe() const override { return {sequence, 1, true}; }
  static ListsWidgetMeta peek(void* context) { return static_cast<Fixture*>(context)->listMeta; }
  static bool readList(void* context, uint32_t rank, ListWidgetEntry& entry) {
    auto& fixture = *static_cast<Fixture*>(context);
    ++fixture.listRows;
    entry = {};
    entry.key = {fixture.listMeta.provenance, rank};
    constexpr const char* names[] = {"Short reads", "Unread fiction", "Added this month"};
    if (rank < 3)
      std::snprintf(entry.name.data(), entry.name.size(), "%s", names[rank]);
    else
      std::snprintf(entry.name.data(), entry.name.size(), "Saved list %u", rank);
    entry.books = rank == 0 ? 84 : rank == 1 ? 132 : 23;
    entry.booksKnown = true;
    return true;
  }
  ListsWidgetSource source() { return {this, peek, readList}; }
  CommandStatus submit(Intent intent) override {
    if (intent != Intent::OpenLastBook) return CommandStatus::Unavailable;
    ++resumes;
    return CommandStatus::Accepted;
  }
  CommandStatus browseCatalog(CatalogRoute destination) override {
    route = destination;
    return offlineLibrary().browseCatalog(destination);
  }
  void readWindow(LibraryWindow& out) const override {
    ++const_cast<Fixture*>(this)->copies;
    out = data;
  }
  static Availability available(CollectionWidgetState state) {
    return state == CollectionWidgetState::Ready || state == CollectionWidgetState::Empty ? Availability::Ready
           : state == CollectionWidgetState::Off                                          ? Availability::Off
           : state == CollectionWidgetState::Unsupported                                  ? Availability::Unsupported
           : state == CollectionWidgetState::Error || state == CollectionWidgetState::Oversize
               ? Availability::Unavailable
               : Availability::NotSaved;
  }
  bool requestBatch(WindowBatchRequest batch) override {
    ++batches;
    data.count = 0;
    data.windowCount = batch.count;
    data.publication = sequence = ++nextSequence;
    data.provenance = {4, 3, 17, 0};
    data.availability = Availability::Ready;
    data.librarySummary = {};
    data.savedLists.clear();
    data.expandedLists.clear();
    data.resume = {};
    for (auto& span : data.windows) span = {};
    for (auto& book : data.rows) book = {};
    summary = {};
    summary.provenance = {4, 3, 17, 0};
    summary.state = States[scene];
    summary.books = {scene == 1 ? 0u : 2418u, true};
    summary.onCard = {scene == 1 ? 0u : 312u, true};
    summary.authors = {scene == 1 ? 0u : 410u, true};
    summary.series = {scene == 1 ? 0u : 92u, true};
    summary.collections = {scene == 1 ? 0u : 9u, true};
    summary.storageKnown = true;
    summary.freeBytes = 4200000000;
    summary.totalBytes = 32000000000;
    summary.bytes = scene == 6 ? LibraryBytes + 1 : 0;
    summary.stale = scene == 7;
    summary.ageKnown = scene == 7;
    summary.ageDays = 2;
    listMeta = {{4, 3, 17, 0},
                scene == 1 ? 0u : 9u,
                scene == 1 ? 0u : 9u,
                scene == 6 ? LibraryBytes + 1 : 0,
                scene == 7 ? 2u : 0u,
                States[scene],
                true,
                true,
                scene == 7,
                scene == 7};
    for (uint8_t i = 0; i < batch.count; ++i) {
      auto& span = data.windows[i];
      span.request = batch.windows[i];
      span.first = data.count;
      span.bytes = sizeof(WindowSpan);
      if (span.request.source == WindowSource::Random || span.request.source == WindowSource::Recent) {
        span.total = scene == 1 ? 0 : 37;
        span.count = scene == 1 ? 0 : 4;
        span.availability = Availability::Ready;
        for (uint8_t n = 0; n < span.count; ++n) {
          auto& book = data.rows[data.count++];
          book.key = {{4, 3, 17, 0}, n};
          std::snprintf(book.title.data(), book.title.size(), "Saved book %u", n);
        }
        span.bytes += span.count * sizeof(BookRow);
        continue;
      }
      if (span.request.source == WindowSource::Continue) {
        auto& book = data.rows[data.count++];
        book.key = {{1, 1, 0, 1}, 0};
        std::strcpy(book.title.data(), "Sibling reading");
        book.readingKnown = true;
        book.page = 36;
        book.pages = 100;
        PersonalCard card;
        card.progress = {36, true};
        projectContinue(true, PersonalState::Ready, card, false, 0, data.resume, nullptr, false, &book);
        span.count = 1;
        span.total = 1;
        span.bytes += sizeof(BookRow) + sizeof(ContinueValues);
        span.availability = Availability::Ready;
        continue;
      }
      ++newWidgetRequests;
      CollectionWidgetState state = CollectionWidgetState::NotSaved;
      if (span.request.source == WindowSource::LibrarySummary) {
        projectLibraryWidget(summary, {17, 3},
                             {static_cast<uint32_t>(span.request.bytes - sizeof(WindowSpan)), span.request.limit},
                             data.librarySummary);
        const auto& value = data.librarySummary;
        state = value.state;
        span.materializedRows = value.rows;
        span.total = value.books;
        span.bytes += value.bytes;
        span.stale = value.stale;
        span.ageKnown = value.ageKnown;
        span.ageDays = value.ageDays;
      } else {
        auto& value = span.request.limit == 1 ? data.savedLists : data.expandedLists;
        projectListsWidget(source(), {17, 3},
                           {static_cast<uint32_t>(span.request.bytes - sizeof(WindowSpan)), span.request.limit}, value);
        state = value.state;
        span.materializedRows = value.count;
        span.total = value.total;
        span.bytes += value.bytes;
        span.stale = value.stale;
        span.ageKnown = value.ageKnown;
        span.ageDays = value.ageDays;
      }
      span.availability = available(state);
      if (state == CollectionWidgetState::Oversize) span.bytes = span.request.bytes + 1;
    }
    return true;
  }
  CatalogSources sourceData{};
  CatalogSummary catalogSummary() const override {
    CatalogSummary value;
    value.provenance = {4, 3, 17, 0};
    value.publication = sequence;
    value.books = {summary.books.value, Availability::Ready};
    value.onCard = {summary.onCard.value, Availability::Ready};
    value.authors = {summary.authors.value, Availability::Ready};
    value.series = {summary.series.value, Availability::Ready};
    value.lists = {listMeta.total, Availability::Ready};
    value.collections = {summary.collections.value, Availability::Ready};
    return value;
  }
  bool requestCatalogSources(CatalogSourceRequest request) override {
    ++listRequests;
    projectSavedListsPage(source(), {17, 3}, {request.offset, LibraryBytes, CatalogSourceRows}, listsPage);
    sourceData = {};
    sourceData.requestId = request.requestId;
    sourceData.publication = sequence = ++nextSequence;
    sourceData.provenance = listsPage.provenance;
    sourceData.total = listsPage.total;
    sourceData.count = listsPage.count;
    sourceData.hasMore = listsPage.hasMore;
    sourceData.availability = available(listsPage.state);
    for (uint8_t i = 0; i < sourceData.count; ++i) {
      sourceData.rows[i].title = listsPage.entries[i].name;
      std::snprintf(sourceData.rows[i].key.data(), 64, "fixture-list-%u", listsPage.entries[i].key.ordinal);
      sourceData.rows[i].books = {listsPage.entries[i].books, Availability::Ready};
    }
    return true;
  }
  void readCatalogSources(CatalogSources& out) const override {
    ++const_cast<Fixture*>(this)->listCopies;
    out = sourceData;
  }
  bool requestListBooks(const QueryRequest& request, ListKey key) override {
    if (key.provenance != listMeta.provenance || key.ordinal >= listMeta.saved) return false;
    return requestQuery(request);
  }
  bool requestQuery(const QueryRequest& request) override {
    ++queryRequests;
    query = request;
    queryData.clear();
    queryData.requestId = request.requestId;
    queryData.provenance = {4, 3, 17, 0};
    queryData.availability = Availability::Ready;
    queryData.matchedBooks = scene == 1 ? 0 : route.destination == CatalogDestination::SavedList ? 84 : 2418;
    queryData.optionTotal = scene == 1 ? 0 : request.filter.group == QueryGroup::Author ? 410 : 92;
    const auto total = request.kind == QueryKind::Groups ? queryData.optionTotal : queryData.matchedBooks;
    queryData.count = request.offset < total ? std::min<uint32_t>(request.limit, total - request.offset) : 0;
    queryData.hasMore = request.offset + queryData.count < total;
    queryData.bytes = sizeof(QueryResult);
    for (uint8_t i = 0; i < queryData.count; ++i) {
      auto& entry = queryData.entries[i];
      entry.book.key = {queryData.provenance, request.offset + i};
      entry.books = 8;
      std::snprintf(entry.book.title.data(), entry.book.title.size(), "Saved book %u", request.offset + i);
      std::snprintf(entry.label.data(), entry.label.size(), "Saved %s %u",
                    request.filter.group == QueryGroup::Author ? "author" : "series", request.offset + i);
      std::snprintf(entry.groupKey.data(), entry.groupKey.size(), "saved-series-%u", request.offset + i);
    }
    queryData.publication = sequence = ++nextSequence;
    return true;
  }
  void readQuery(QueryResult& out) const override {
    ++const_cast<Fixture*>(this)->queryCopies;
    out = queryData;
  }
  CoverReference coverReference(BookKey key) const override { return {key.ordinal + 1, CoverWidth, CoverHeight}; }
  bool readCover(CoverReference, CoverBitmap& out) const override {
    ++const_cast<Fixture*>(this)->covers;
    out.width = CoverWidth;
    out.height = CoverHeight;
    for (size_t i = 0; i < out.pixels.size(); ++i) out.pixels[i] = i % 5 ? 0x55 : 0;
    return true;
  }
  CommandStatus requestCover(BookKey) override {
    ++coverIntents;
    return CommandStatus::Unavailable;
  }
};
inline void capture(Fixture& fixture, const char* output, const std::string& name) {
  const unsigned before[] = {fixture.copies,   fixture.queryCopies, fixture.listCopies,
                             fixture.listRows, fixture.covers,      fixture.coverIntents};
  if (activityManager.requestUpdateAndWait() != RequestUpdateResult::Rendered) std::_Exit(111);
  RenderLock lock;
  const unsigned after[] = {fixture.copies,   fixture.queryCopies, fixture.listCopies,
                            fixture.listRows, fixture.covers,      fixture.coverIntents};
  for (uint8_t i = 0; i < 6; ++i)
    if (before[i] != after[i]) std::_Exit(112);
  std::filesystem::create_directories(output);
  const auto path = std::filesystem::path(output) / (name + ".pgm");
  FILE* file = std::fopen(path.c_str(), "wb");
  if (!file) std::_Exit(113);
  std::fprintf(file, "P5\n%d %d\n255\n", renderer.getScreenWidth(), renderer.getScreenHeight());
  for (int y = 0; y < renderer.getScreenHeight(); ++y)
    for (int x = 0; x < renderer.getScreenWidth(); ++x) std::fputc(renderer.isPixelBlack(x, y) ? 0 : 255, file);
  std::fclose(file);
}
inline bool tick() {
  const auto* output = std::getenv("BOOKSYNC_COLLECTION_CARDS_CAPTURES");
  if (!output) return false;
  mappedInputManager.simulatorClearInputFrame();
  static Fixture fixtures[2];
  static unsigned frame = 0, cycle = 0;
  const bool text = !std::strcmp(Layout::deviceClass(), "x3");
  const bool extra = cycle >= 32;
  const unsigned extraIndex = extra ? cycle - 32 : 0;
  const unsigned kind = extra ? (extraIndex < 3 ? 0 : 1) : cycle / 16;
  const unsigned expanded = extra ? (extraIndex == 3 ? 0 : 1) : cycle % 16 / 8;
  const unsigned state = extra ? 0 : cycle % 8;
  const uint8_t target = extra ? (extraIndex < 3 ? extraIndex + 1 : extraIndex == 3 ? 1 : 3) : 0;
  auto& fixture = fixtures[cycle % 2];
  if (frame++ == 0) {
    fixture.page = {};
    fixture.page.revision = 1;
    fixture.page.count = text ? 3 : 2;
    fixture.page.rows = text ? 7 : 6;
    fixture.page.textOnly = text;
    fixture.scene = state;
    fixture.page.widgets[0] = {kind ? WidgetKind::Lists : WidgetKind::Library,
                               WindowSource::OnDevice,
                               0,
                               0,
                               static_cast<uint8_t>(expanded ? 2 : 1),
                               static_cast<uint8_t>(expanded ? 2 : 1),
                               0};
    fixture.page.widgets[1] = {
        WidgetKind::Continue, WindowSource::Continue, 0, static_cast<uint8_t>(text ? 0 : 2), 2, 2, 0};
    if (text) fixture.page.widgets[2] = {WidgetKind::Lists, WindowSource::OnDevice, 1, 4, 1, 1, 0};
    fixture.batches = fixture.copies = fixture.queryRequests = fixture.listRequests = fixture.queryCopies =
        fixture.listCopies = fixture.listRows = fixture.resumes = fixture.covers = fixture.coverIntents =
            fixture.newWidgetRequests = 0;
    showHomeScene(renderer, mappedInputManager, fixture);
  }
  const std::string name =
      text    ? "new-widgets-off"
      : extra ? "destination-" + std::to_string(extraIndex)
              : std::string(kind ? "lists" : "library") + (expanded ? "-optional-" : "-main-") + StateNames[state];
  if (frame == 28) capture(fixture, output, name);
  const bool navigate = !text && (extra || state == 0 || state == 1 || state == 7);
  bool touchNavigation = false;
#if CROSSINK_APP_CAP_TOUCH
  touchNavigation = mappedInputManager.hasTouch();
  if (navigate && touchNavigation && (frame == 36 || frame == 38)) {
    int topInset, rightInset, bottomInset, leftInset;
    renderer.getOrientedViewableTRBL(&topInset, &rightInset, &bottomInset, &leftInset);
    const auto tile = CompactGrid::forHomePanel(renderer.getScreenWidth(), renderer.getScreenHeight(), topInset,
                                                rightInset, bottomInset, leftInset)
                          .compactTile(fixture.page.widgets[0]);
    const WidgetBox box{tile.width, tile.height, renderer.getLineHeight(UI_10_FONT_ID), false, true};
    WidgetCell targetCell;
    const bool found = kind ? widgets::ListsCard{}.listTarget(box, target, targetCell)
                            : widgets::LibraryCard{}.libraryTarget(box, target, targetCell);
    if (!found) std::_Exit(115);
    const int x = tile.x + targetCell.x + targetCell.width / 2, y = tile.y + targetCell.y + targetCell.height / 2;
    if (frame == 36)
      mappedInputManager.simulatorInjectTouchDown(x, y);
    else
      mappedInputManager.simulatorInjectTouchRelease(x, y);
  }
#endif
  if (!touchNavigation && navigate && target && frame >= 30 && frame <= 34 && !(frame % 2) && (frame - 30) / 2 < target)
    mappedInputManager.simulatorInjectRelease(mappedInputManager.menuButton(MappedInputManager::Button::Down));
  if (frame == 38 && ((!touchNavigation && navigate) || (!text && (state == 5 || state == 6))))
    mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Confirm);
  if (!text && (state == 5 || state == 6)) {
    if (frame == 44)
      mappedInputManager.simulatorInjectRelease(mappedInputManager.menuButton(MappedInputManager::Button::Down));
    if (frame == 46) mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Confirm);
  }
  if (navigate && frame == 60) capture(fixture, output, name + "-destination");
  if (navigate && frame == 62) mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Back);
  if (frame < 84) return true;
  const bool dashboard = navigate && kind == 0 && target == 0;
  // F36a's approved dashboard owns one bounded Random/Recent preview batch.
  const unsigned expected = dashboard ? 3 : navigate || (!text && (state == 5 || state == 6)) ? 2 : 1;
  if (fixture.batches != expected || fixture.copies != expected ||
      fixture.resumes != (!text && (state == 5 || state == 6) ? 1u : 0u) ||
      (text && (fixture.newWidgetRequests || fixture.covers || fixture.coverIntents)) ||
      (navigate && fixture.queryRequests + fixture.listRequests != (dashboard ? 0u : 1u))) {
    LOG_ERR("BSWC", "Collection scene %u failed: batches=%u copies=%u query=%u lists=%u resumes=%u", cycle,
            fixture.batches, fixture.copies, fixture.queryRequests, fixture.listRequests, fixture.resumes);
    std::fflush(nullptr);
    std::_Exit(114);
  }
  if (navigate) capture(fixture, output, name + "-back");
  frame = 0;
  if (++cycle == (text ? 1u : 37u)) {
    LOG_INF(
        "BSWC",
        "BookSync collection cards PASS: %u scenes, both sizes, eight states, source destinations and sibling Resume",
        cycle);
    std::fflush(nullptr);
    std::_Exit(0);
  }
  return true;
}
}  // namespace collectionCardsScenario
