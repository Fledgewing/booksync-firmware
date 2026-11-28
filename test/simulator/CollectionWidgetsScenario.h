// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>

#include "booksync/services/widgets/Collections.h"
namespace collectionWidgetsScenario {
using namespace booksync;
struct Fixture {
  ListsWidgetMeta meta{{4, 3, 17, 0}, 9, 9, 0, 0, PersonalState::Ready, true, true, false, false};
  unsigned reads = 0, peeks = 0;
  bool changed = false;
  static ListsWidgetMeta peek(void* context) {
    auto& f = *static_cast<Fixture*>(context);
    auto meta = f.meta;
    if (++f.peeks > 1 && f.changed) ++meta.provenance.accountEpoch;
    return meta;
  }
  static bool read(void* context, uint32_t rank, ListWidgetEntry& entry) {
    auto& f = *static_cast<Fixture*>(context);
    ++f.reads;
    entry = {};
    entry.key = {f.meta.provenance, rank};
    entry.books = 84 + rank;
    entry.booksKnown = true;
    std::snprintf(entry.name.data(), entry.name.size(), "Synthetic list %u", rank);
    return true;
  }
  ListsWidgetSource source() { return {this, peek, read}; }
};
inline bool run() {
  if (!std::getenv("BOOKSYNC_COLLECTION_WIDGET_PROJECTION_SCENARIO")) return false;
  static LibraryWidgetInput input;
  static LibraryWidgetValues library;
  static ListsWidgetValues lists;
  static Fixture fixture;
  unsigned checks = 0;
  auto require = [&](bool condition) {
    ++checks;
    if (!condition) {
      LOG_ERR("BSWC", "Collection projection check %u failed", checks);
      std::fflush(nullptr);
      std::_Exit(106);
    }
  };
  input.provenance = {4, 3, 17, 0};
  input.state = PersonalState::Ready;
  input.books = {2418, true};
  input.onCard = {312, true};
  input.storageKnown = true;
  input.freeBytes = 4200000000;
  input.totalBytes = 32000000000;
  require(projectLibraryWidget(input, {17, 3}, {LibraryBytes, 2}, library));
  require(library.books == 2418 && library.onCard == 312 && library.rows == 2);
  require(!projectLibraryWidget(input, {17, 3}, {LibraryBytes, 1}, library));
  require(library.state == CollectionWidgetState::Oversize);
  require(projectLibraryWidget(input, {17, 4}, {LibraryBytes, 2}, library));
  require(!library.booksKnown && library.state == CollectionWidgetState::NotSaved);
  input.stale = true;
  input.ageKnown = true;
  input.ageDays = 2;
  require(projectLibraryWidget(input, {17, 3}, {LibraryBytes, 2}, library));
  require(library.stale && library.ageDays == 2);
  input.books = {0, true};
  input.onCard = {0, true};
  require(projectLibraryWidget(input, {17, 3}, {LibraryBytes, 2}, library));
  require(library.state == CollectionWidgetState::Empty && library.booksKnown);
  for (uint16_t rows : {uint16_t(1), uint16_t(3)}) {
    fixture.reads = fixture.peeks = 0;
    require(projectListsWidget(fixture.source(), {17, 3}, {LibraryBytes, rows}, lists));
    require(lists.count == rows && fixture.reads == rows && fixture.peeks == 2 && lists.total == 9);
  }
  fixture.peeks = fixture.reads = 0;
  fixture.changed = true;
  require(projectListsWidget(fixture.source(), {17, 3}, {LibraryBytes, 3}, lists));
  require(lists.state == CollectionWidgetState::NotSaved && lists.count == 0 && lists.entries[0].name[0] == 0);
  fixture.changed = false;
  constexpr PersonalState states[] = {PersonalState::NotSaved, PersonalState::Off, PersonalState::Unsupported,
                                      PersonalState::Unavailable, PersonalState::Oversized};
  for (auto state : states) {
    fixture.meta.state = state;
    fixture.peeks = fixture.reads = 0;
    projectListsWidget(fixture.source(), {17, 3}, {LibraryBytes, 3}, lists);
    require(lists.count == 0 && fixture.reads == 0);
  }
  LOG_INF("BSWC", "BookSync collection widgets projection PASS: %u checks, two Library rows and one/three Lists rows",
          checks);
  std::fflush(nullptr);
  std::_Exit(0);
}
}  // namespace collectionWidgetsScenario
