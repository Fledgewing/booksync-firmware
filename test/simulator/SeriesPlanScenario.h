// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdlib>
#include <cstring>

#include "booksync/services/SeriesPlan.h"
inline void runSeriesPlanScenario() {
  const char* mode = std::getenv("BOOKSYNC_SERIES_PLAN_SCENARIO");
  if (!mode) return;
  using namespace booksync;
  struct Source {
    QueryState state{{1, 1, 7, 1}, 12, QueryAllFields, true};
    uint64_t revision = 1;
    uint32_t reads = 0;
  };
  static Source source;
  static SeriesPlan worker(
      {&source, [](void* p) { return static_cast<Source*>(p)->state; },
       [](void* p, BookKey key, QueryRecord& out) {
         ++static_cast<Source*>(p)->reads;
         out.clear();
         out.book.key = key;
         out.seriesCount = 2;
         std::strcpy(out.seriesKeys[0].data(), "primary:other");
         std::strcpy(out.seriesKeys[1].data(), "secondary:series");
         std::strcpy(out.series[1].data(), "Synthetic series");
         out.seriesPositions[1] = key.ordinal / 2.0;
         out.seriesPositionsKnown[1] = true;
         out.seriesRanks[1] = key.ordinal;
         out.authorCount = 1;
         std::strcpy(out.authors[0].data(), "Example Press");
         out.status = key.ordinal == 3 ? QueryShow::Reading : key.ordinal < 2 ? QueryShow::Finished : QueryShow::Unread;
         out.onDevice = QueryTruth::No;
         return true;
       }},
      {&source, [](void* p) { return static_cast<Source*>(p)->revision; },
       [](void*, BookKey key) { return key.ordinal == 5 ? QueryTruth::Yes : QueryTruth::No; }});
  static SeriesRequest request;
  static SeriesPlanView view;
  std::strcpy(request.key.data(), "secondary:series");
  request.requestId = 1;
  if (!std::strcmp(mode, "gap")) {
    request.startKnown = true;
    request.start = {source.state.provenance, 2};
  }
  if (!std::strcmp(mode, "bounded")) {
    source.state.count = 10000;
    request.count = 10;
  }
  if (!std::strcmp(mode, "incomplete")) source.state.fields &= ~queryField(QueryField::SeriesIdentity);
  bool ok = worker.request(request);
  unsigned ticks = 0;
  while (worker.busy() && ticks++ < 1500) {
    worker.tick();
    ok = ok && worker.lastWork() <= SeriesPlan::Batch;
    if (!std::strcmp(mode, "account")) ++source.state.provenance.accountEpoch;
    if (!std::strcmp(mode, "jobs")) ++source.revision;
  }
  worker.copyPlan(view);
  if (!std::strcmp(mode, "account") || !std::strcmp(mode, "jobs"))
    ok = ok && view.availability == Availability::Unavailable && view.token == 0 && view.count == 0;
  else if (!std::strcmp(mode, "incomplete"))
    ok = ok && view.availability == Availability::Unsupported && view.membershipIncomplete && view.count == 0;
  else {
    ok = ok && view.availability == Availability::Ready && view.token != 0;
    if (!std::strcmp(mode, "gap"))
      ok = ok && view.next[0].book.key.ordinal == 2 && !view.earlierKnown;
    else
      ok = ok && view.next[0].book.key.ordinal == 4 && view.next[1].book.key.ordinal == 6 && view.earlierKnown;
    if (!std::strcmp(mode, "bounded")) ok = ok && view.count == 10 && source.reads == 20000;
  }
  const auto before = source.reads;
  for (unsigned i = 0; i < 100; ++i) worker.copyPlan(view);
  ok = ok && before == source.reads && !worker.busy();
  LOG_INF("BSS", "BookSync Series plan %s: %s; workspace=%u; result=%u; reads=%u; per-tick=%u; payload allocations=0",
          ok ? "PASS" : "FAIL", mode, unsigned(sizeof(worker)), unsigned(sizeof(view)), source.reads,
          SeriesPlan::Batch);
  std::fflush(nullptr);
  std::_Exit(ok ? 0 : 1);
}
