// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>

#include "booksync/services/NativeHistory.h"
#include "booksync/services/SdIndexSource.h"
inline void runDeviceIndexScenario() {
  if (!std::getenv("BOOKSYNC_INDEX_SCENARIO")) return;
  using namespace booksync;
  // Simulator-only static workspaces avoid placing windows/codecs on boot stack.
  static storage::Index index({"/local-index", "/local-index.tmp", "/local-index.bak"}, "/local-a", "/local-b");
  static ReadModel model(index);
  static DeviceIndex worker(index, model);
  static SdIndexSource source;
  bool ok = index.recover() && !index.ready();
  unsigned coldTicks = 0, warmTicks = 0;
  for (unsigned generation = 1; generation <= 2 && ok; ++generation) {
    ok = source.start() && worker.start(source.source(), {generation, 1, 0, 0});
    unsigned ticks = 0;
    while (worker.busy() && ok) {
      ok = worker.tick() && worker.lastWork() <= 16;
      ++ticks;
    }
    source.close();
    if (generation == 1)
      coldTicks = ticks;
    else
      warmTicks = ticks;
    ok = ok && index.count() == 10000 && model.materialize({0, 24, 16384});
  }
  WindowBatchRequest batch;
  batch.count = 3;
  batch.windows[0] = {0, 1, 16384};
  batch.windows[1] = {100, 11, 16384};
  batch.windows[2] = {9980, 12, 16384};
  ok = ok && model.materializeBatch(batch) && model.window().windowCount == 3 && model.window().count == 24;
  static LibraryWindow snapshot;
  snapshot = model.window();
  ok = ok && snapshot.count == 24 && snapshot.bytes <= 16384 && snapshot.availability == Availability::Ready;
  // Immutable copied value remains valid after invalidation; no paint I/O.
  auto subscription = model.subscribe();
  model.invalidate(Invalidation::Capture);
  ok = ok && !snapshot.stale && model.changed(subscription) && !model.changed(subscription);
  // A bounded interrupted generation cannot displace either warm catalog.
  ok = ok && source.start() && worker.start(source.source(), {3, 1, 0, 0}) && worker.tick();
  worker.cancel();
  source.close();
  static storage::Index restarted({"/local-index", "/local-index.tmp", "/local-index.bak"}, "/local-a", "/local-b");
  ok = ok && restarted.recover() && restarted.count() == 10000 && restarted.provenance().generation == 2;
  // Stream 50k visits with the same fixed cursor budget used by the native source.
  struct Visits {
    unsigned n = 0;
  } visits;
  HistorySource history{[](void* p, ReadingVisit& value) {
                          auto& n = static_cast<Visits*>(p)->n;
                          if (n == 50000) return HistoryStep::End;
                          value.path = "/books/00000.epub";
                          value.sample = {};
                          value.sample.totalSeconds = n;
                          value.revision = ++n;
                          return HistoryStep::Record;
                        },
                        &visits};
  ok = ok && model.materialize({0, 24, 16384}) && model.beginHistory(history);
  unsigned historyWindows = 0;
  while (ok && model.historyBusy()) {
    ok = model.historyTick() && model.lastHistoryWork() <= 256;
    ++historyWindows;
  }
  ok = ok && historyWindows == 197 && model.window().provenance.capture == 50000;
  // Real F09 capture and journal projection, without changing reader behavior.
  static storage::LifecycleJournal journal(
      {"/.booksync/reader.journal", "/.booksync/reader.journal.tmp", "/.booksync/reader.journal.bak"});
  Storage.mkdir("/.booksync");
  reader::Capture capture(journal);
  reader::Sample sample;
  sample.totalSeconds = 123;
  sample.page = 4;
  sample.pages = 20;
  ok = ok && journal.recover() == storage::JournalResult::Ok &&
       capture.open("/books/00000.epub", 1) == storage::JournalResult::Ok &&
       capture.update(sample, true) == storage::JournalResult::Ok;
  static NativeHistory native;
  ok = ok && native.start() && model.beginHistory(native.source());
  while (ok && model.historyBusy()) ok = model.historyTick();
  bool found = false;
  snapshot = model.window();
  for (unsigned i = 0; i < snapshot.count; ++i)
    if (snapshot.rows[i].readingKnown && snapshot.rows[i].seconds == 123) found = true;
  // Directory enumeration order is deliberately unspecified; the target may not
  // be in this first page. Resolve it in a separate requested worker window below.
  unsigned target = 0;
  static storage::IndexRecord record;
  for (; target < index.count(); ++target) {
    if (!index.read({index.provenance(), target}, record)) {
      ok = false;
      break;
    }
    if (std::string_view(record.path.data()) == "/books/00000.epub") break;
  }
  ok = ok && target < index.count() && model.materialize({target, 1, 16384}) && native.start() &&
       model.beginHistory(native.source());
  while (ok && model.historyBusy()) ok = model.historyTick();
  snapshot = model.window();
  found = snapshot.count == 1 && snapshot.rows[0].readingKnown && snapshot.rows[0].seconds == 123;
  ok = ok && found && index.read(snapshot.rows[0].key, record);
  LOG_INF(
      "BSI",
      "BookSync index scenario %s: 10000 books; cold=%u warm=%u; history=50000/%u windows; snapshot=%u worker=%u bytes",
      ok ? "PASS" : "FAIL", coldTicks, warmTicks, historyWindows, unsigned(sizeof(LibraryWindow)),
      unsigned(sizeof(DeviceIndex) + sizeof(ReadModel) + sizeof(SdIndexSource) + sizeof(storage::Index) +
               sizeof(NativeHistory)));
  std::fflush(nullptr);
  std::_Exit(ok ? 0 : 1);
}
