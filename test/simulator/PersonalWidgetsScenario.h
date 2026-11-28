// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "booksync/services/widgets/PersonalWidgets.h"
namespace personalWidgetsScenario {
using namespace booksync;
struct Source final : PersonalReadingSource {
  PersonalState state = PersonalState::Ready;
  uint32_t reads = 0;
  ReadingScope owner() const override { return {}; }
  PersonalState availability(PersonalKind) const override { return state; }
  PersonalStep read(PersonalKind, uint32_t n, PersonalRecord& out) override {
    ++reads;
    if (n >= 5) return PersonalStep::End;
    constexpr const char* names[] = {"alpha", "beta", "gamma", "delta", "epsilon"};
    out = {};
    std::strcpy(out.identity.data(), names[n]);
    std::strcpy(out.book.title.data(), names[n]);
    out.book.key = {{9, 1, 0, 3}, n};
    return PersonalStep::Record;
  }
  PersonalStep find(PersonalKind, std::string_view id, uint32_t& ordinal) override {
    constexpr const char* names[] = {"alpha", "beta", "gamma", "delta", "epsilon"};
    for (uint32_t i = 0; i < 5; ++i)
      if (id == names[i]) {
        ordinal = i;
        return PersonalStep::Record;
      }
    return PersonalStep::End;
  }
};
inline bool run() {
  if (!std::getenv("BOOKSYNC_PERSONAL_WIDGET_PROJECTION_SCENARIO")) return false;
  static Source source;
  static PersonalReadingSource* sources[] = {&source};
  static PersonalReading personal(sources, 1);
  static PersonalWidgetValues out;
  static DiscoverProjection discover(personal, out);
  PersonalWidgetCapture capture{{9, 1, 0, 3}, {}, true, true, 2};
  unsigned checks = 0;
  auto require = [&](bool value) {
    ++checks;
    if (!value) {
      LOG_ERR("BSWP", "Personal widget projection check %u failed", checks);
      std::fflush(nullptr);
      std::_Exit(107);
    }
  };
  require(!discover.begin(200, 0, capture, {1, LibraryBytes}));
  require(out.state == PersonalWidgetState::Oversize && !source.reads);
  require(discover.begin(200, 0, capture, {4, LibraryBytes}));
  unsigned ticks = 0;
  while (discover.busy() && ++ticks < 100000) {
    const auto before = source.reads;
    require(discover.tick(capture));
    require(source.reads - before <= 32);
    if (discover.busy()) require(!out.count);
  }
  require(!discover.busy() && out.state == PersonalWidgetState::Ready && out.count == 4 && out.rows == 4 &&
          out.total == 5);
  require(out.stale && out.ageKnown && out.ageDays == 2);
  constexpr const char* order[] = {"beta", "delta", "epsilon", "alpha"};
  for (uint8_t i = 0; i < 4; ++i) require(!std::strcmp(out.books[i].title.data(), order[i]));
  ++capture.provenance.capture;
  require(discover.tick(capture));
  require(!out.count && !out.books[0].title[0]);
  constexpr PersonalState states[] = {PersonalState::NotSaved,    PersonalState::Empty,       PersonalState::Off,
                                      PersonalState::Unsupported, PersonalState::Unavailable, PersonalState::Oversized};
  for (auto state : states) {
    source.state = state;
    require(discover.begin(200, 0, capture, {4, LibraryBytes}));
    for (unsigned i = 0; discover.busy() && i < 100; ++i) require(discover.tick(capture));
    require(!out.count && out.state != PersonalWidgetState::Ready);
  }
  source.state = PersonalState::Ready;
  require(personal.begin(PersonalKind::Recommended, 200, 0, {}));
  for (unsigned i = 0; personal.busy() && i < 100; ++i) require(personal.tick());
  require(!personal.busy());
  require(projectRecommendedWidget(personal.result(), capture, {1, LibraryBytes}, out));
  require(out.count == 1 && out.rows == 1 && out.total == 5 && !std::strcmp(out.books[0].title.data(), "alpha"));
  LOG_INF("BSWP", "BookSync personal widget projection PASS: %u checks, four Discover rows and one Recommended row",
          checks);
  std::fflush(nullptr);
  std::_Exit(0);
}
}  // namespace personalWidgetsScenario
