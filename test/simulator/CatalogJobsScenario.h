// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../fixtures/catalog/FakeServer.h"
#include "booksync/services/CatalogJobs.h"
inline void runCatalogJobsScenario() {
  const char* mode = std::getenv("BOOKSYNC_CATALOG_JOBS_SCENARIO");
  if (!mode) return;
  using namespace booksync;
  constexpr storage::ScopeView scope{"fixture-server-token", "fixture-account-token"};
  static storage::ProjectionStore store(7, 1, scope);
  static ReadModel model(store.index());
  static Admission admission({nullptr, [](void*) { return MemorySnapshot{128 * 1024, 64 * 1024, 0}; },
                              [](void*) { return true; }, [](void*) { return true; }, [](void*) { return true; }},
                             true);
  static CatalogFakeServer transport;
  transport.total = 80;
  static server::Catalog source(transport);
  static CatalogJobs jobs(store, model, admission);
  admission.setReading(false);
  bool ok = Storage.mkdir("/.booksync") || Storage.exists("/.booksync");
  ok = ok && store.recover();
  if (!std::strcmp(mode, "recover") || !std::strcmp(mode, "recover-new")) {
    const unsigned expected = !std::strcmp(mode, "recover") ? 1 : 2;
    ok =
        ok && store.index().ready() && store.index().count() == 80 && store.index().provenance().generation == expected;
    static CatalogRow row;
    ok = ok && store.read({store.index().provenance(), 79}, row) && !std::strcmp(row.alias.data(), "80");
    static storage::ProjectionStore other(7, 1, {"fixture-server-token", "other-account-token"}), later(8, 3, scope);
    ok = ok && other.recover() && later.recover() && !other.index().ready() && later.index().ready();
  } else {
    CatalogRequest request{&source, {[](void* p) { return static_cast<server::Catalog*>(p)->start({}); }, &source},
                           nullptr, nullptr,
                           {7, 1},  scope};
    const auto run = ok ? jobs.start(request, {7, 1}) : 0;
    ok = ok && run && jobs.start(request, {7, 1}) == run;
    unsigned paints = 0;
    Delivery home({7, 1}, 0, DeliveryPolicy::Owner,
                  {[](void* p, DeliveryResult) { ++*static_cast<unsigned*>(p); }, &paints});
    home.retire();
    unsigned ticks = 0;
    while (ok && jobs.busy()) {
      if ((!std::strcmp(mode, "cut-begin") && ticks == 0) || (!std::strcmp(mode, "cut-stream") && ticks == 3)) {
        LOG_INF("BSC", "BookSync catalog jobs intentional power cut; tick=%u", ticks);
        std::fflush(nullptr);
        std::_Exit(73);  // No destructors: restart reselects the last committed generation.
      }
      ok = jobs.tick({7, 1}) && jobs.lastWork() <= CatalogJobs::Batch;
      ++ticks;
      if (!std::strcmp(mode, "cancel") && ticks == 1) jobs.cancel(run);
    }
    const bool canceled = !std::strcmp(mode, "cancel");
    ok = ok && jobs.result(run, {7, 1}) == (canceled ? DeliveryResult::Canceled : DeliveryResult::Ready);
    home.poll(200000, jobs.result(run, {7, 1}), {7, 1});
    ok = ok && paints == 0 && !admission.busy();
    if (canceled) ok = ok && store.index().provenance().generation == 1;
    ok = ok && model.materialize({}) && model.availability() == Availability::Ready;
  }
  LOG_INF("BSC", "BookSync catalog jobs scenario %s: %s; workspace=%u store=%u; batch=%u; per-row allocations=0",
          ok ? "PASS" : "FAIL", mode, unsigned(sizeof(jobs)), unsigned(sizeof(store)), unsigned(CatalogJobs::Batch));
  std::fflush(nullptr);
  std::_Exit(ok ? 0 : 1);
}
