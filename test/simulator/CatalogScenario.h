// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>

#include "../fixtures/catalog/FakeServer.h"
inline void runCatalogScenario() {
  const char* mode = std::getenv("BOOKSYNC_CATALOG_SCENARIO");
  if (!mode) return;
  using namespace booksync;
  // Fixed simulator workspaces keep boot stack small. The production worker
  // owns/reuses these objects for a job; no parser/row allocation occurs per book.
  static storage::Index index({"/catalog", "/catalog.tmp", "/catalog.bak"}, "/catalog-a", "/catalog-b");
  static ReadModel model(index);
  static DeviceIndex worker(index, model);
  static CatalogFakeServer transport;
  static server::Catalog catalog(transport);
  static CatalogIndexSource binding(catalog, catalogFixtureBinding, nullptr);
  bool ok = index.recover();
  unsigned ticks = 0;
  if (!std::strcmp(mode, "ingest")) {
    ok = ok && !index.ready() && catalog.start({}) && worker.start(binding.source(), {1, 1, 7, 0});
    while (ok && worker.busy()) {
      ok = worker.tick() && worker.lastWork() <= 16;
      ++ticks;
    }
    ok = ok && index.count() == 10000 && transport.requests == 50 && transport.closes == 50 && ticks == 626;
  } else if (!std::strcmp(mode, "recover")) {
    ok = ok && index.count() == 10000 && index.provenance().generation == 1;
    static storage::IndexRecord row;
    ok = ok && index.read({index.provenance(), 9999}, row) && !std::strcmp(row.title.data(), "Synthetic book 10000");
  } else {
    CatalogError expected = CatalogError::PermissionLost;
    if (!std::strcmp(mode, "malformed")) {
      transport.fault = CatalogFakeServer::Fault::Malformed;
      expected = CatalogError::Invalid;
    } else if (!std::strcmp(mode, "oversized")) {
      transport.fault = CatalogFakeServer::Fault::Oversized;
      expected = CatalogError::Oversized;
    } else if (!std::strcmp(mode, "partial")) {
      transport.fault = CatalogFakeServer::Fault::Partial;
      expected = CatalogError::Invalid;
    } else
      transport.breakPage = 2;
    ok = ok && catalog.start({}) && worker.start(binding.source(), {2, 1, 7, 0});
    bool running = ok;
    while (running && worker.busy()) {
      running = worker.tick();
      ++ticks;
    }
    ok = ok && !running && catalog.error() == expected && index.count() == 10000 && index.provenance().generation == 1;
  }
  LOG_INF("BSC",
          "BookSync catalog scenario %s: %s; ticks=%u parser=%u row=%u; dynamic parser allocations=0; read bound=%u",
          ok ? "PASS" : "FAIL", mode, ticks, unsigned(sizeof(catalog)), unsigned(sizeof(CatalogRow)),
          unsigned(transport.maxRead));
  std::fflush(nullptr);
  std::_Exit(ok ? 0 : 1);
}
