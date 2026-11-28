// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>

#include "booksync/services/widgets/BookList.h"
namespace bookListProjectionScenario {
using namespace booksync;
struct Fixture {
  BookListMeta meta{};
  unsigned reads = 0;
  int failAt = -1;
  bool changed = false;
  unsigned peeks = 0;
  BookListSource source() { return {this, peek, read}; }
  static BookListMeta peek(void* context, WindowSource) {
    auto& f = *static_cast<Fixture*>(context);
    auto meta = f.meta;
    if (++f.peeks > 1 && f.changed) ++meta.provenance.accountEpoch;
    return meta;
  }
  static bool read(void* context, WindowSource, uint32_t rank, BookRow& row) {
    auto& f = *static_cast<Fixture*>(context);
    ++f.reads;
    if (int(rank) == f.failAt) return false;
    row = {};
    row.key = {f.meta.provenance, 40 - rank * 3};
    std::snprintf(row.title.data(), row.title.size(), "Synthetic book %u", row.key.ordinal);
    return true;
  }
  void reset() {
    meta = {};
    meta.provenance = {4, 3, 17, 0};
    meta.state = PersonalState::Ready;
    meta.total = meta.saved = 10000;
    meta.countKnown = meta.complete = true;
    meta.year = 2026;
    reads = peeks = 0;
    failAt = -1;
    changed = false;
  }
};
inline bool run() {
  if (!std::getenv("BOOKSYNC_BOOKLIST_PROJECTION_SCENARIO")) return false;
  static Fixture fixture;
  static BookListProjection out, sibling;
  unsigned checks = 0;
  auto require = [&](bool condition) {
    ++checks;
    if (!condition) {
      LOG_ERR("BSBL", "Book list projection check %u failed", checks);
      std::fflush(nullptr);
      std::_Exit(91);
    }
  };
  constexpr WindowSource presets[] = {WindowSource::AlsoReading,  WindowSource::Recent,     WindowSource::WantToRead,
                                      WindowSource::AllFinished,  WindowSource::PickBackUp, WindowSource::UpNext,
                                      WindowSource::FinishedYear, WindowSource::Unopened,   WindowSource::OnDevice};
  WindowRequest request{0, 4, LibraryBytes, WindowSource::Recent};
  for (auto preset : presets) {
    fixture.reset();
    request.source = preset;
    require(projectBookList(fixture.source(), request, {17, 3}, 2026, out));
    const unsigned rows = preset == WindowSource::AlsoReading || preset == WindowSource::OnDevice ? 3 : 4;
    require(out.state == PersonalState::Ready && out.count == rows && fixture.reads == rows && out.total == 10000);
    require(out.books[0].key.ordinal == 40 && out.books[1].key.ordinal == 37);
  }
  request.source = WindowSource::Recent;
  constexpr PersonalState states[] = {PersonalState::Empty,       PersonalState::NotSaved,    PersonalState::Off,
                                      PersonalState::Unsupported, PersonalState::Unavailable, PersonalState::Oversized};
  for (auto state : states) {
    fixture.reset();
    fixture.meta.state = state;
    if (state == PersonalState::Empty) fixture.meta.saved = fixture.meta.total = 0;
    projectBookList(fixture.source(), request, {17, 3}, 2026, out);
    require(out.state == state && out.count == 0 && fixture.reads == 0);
  }
  fixture.reset();
  fixture.meta.stale = true;
  require(projectBookList(fixture.source(), request, {17, 3}, 2026, sibling));
  require(sibling.stale && sibling.count == 4 && sibling.total == 10000);
  fixture.failAt = 2;
  require(!projectBookList(fixture.source(), request, {17, 3}, 2026, out));
  require(out.count == 0 && out.books[0].title[0] == 0 && sibling.count == 4);
  fixture.reset();
  fixture.changed = true;
  projectBookList(fixture.source(), request, {17, 3}, 2026, out);
  require(out.state == PersonalState::NotSaved && out.count == 0 && out.total == 0);
  fixture.reset();
  fixture.meta.bytes = LibraryBytes + 1;
  require(!projectBookList(fixture.source(), request, {17, 3}, 2026, out));
  require(out.state == PersonalState::Oversized && fixture.reads == 0);
  fixture.reset();
  request.source = WindowSource::FinishedYear;
  projectBookList(fixture.source(), request, {17, 3}, 2027, out);
  require(out.state == PersonalState::NotSaved && fixture.reads == 0);
  LOG_INF("BSBL", "BookSync Book list projection PASS: %u checks; nine presets; workspace=%u", checks,
          unsigned(sizeof(BookListProjection)));
  std::fflush(nullptr);
  std::_Exit(0);
}
}  // namespace bookListProjectionScenario
