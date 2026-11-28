// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "booksync/services/SeriesQueue.h"
inline void runSeriesQueueScenario() {
  const char* mode = std::getenv("BOOKSYNC_SERIES_QUEUE_SCENARIO");
  if (!mode) return;
  using namespace booksync;
  struct State {
    bool online = true, writable = true, current = true;
    unsigned links = 0;
  };
  static State state;
  static Admission admission({nullptr, [](void*) { return MemorySnapshot{128 * 1024, 64 * 1024, 0}; },
                              [](void*) { return true; }, [](void*) { return true; }, [](void*) { return true; }},
                             true);
  struct Source final : DownloadSource {
    uint64_t position = 0;
    unsigned opens = 0;
    bool bad = false;
    bool open(const DownloadSpec& s, uint64_t offset, const char*, DownloadReply& r) override {
      ++opens;
      position = offset;
      r = {};
      r.identity = s.identity;
      r.offset = offset;
      r.bytes = 3;
      r.sizeKnown = true;
      std::strcpy(r.validator.data(), "strong-revision");
      return true;
    }
    int read(void* out, size_t) override {
      if (position == 3) return 0;
      *static_cast<char*>(out) = bad ? 'x' : "abc"[position++];
      if (bad) ++position;
      return 1;
    }
    bool finish() override { return true; }
    void close() override {}
  };
  static Source source;
  static storage::SeriesQueueStore store({"/series/index", "/series/index.tmp", "/series/index.bak"}, "/series/queue",
                                         {"fixture-server", "fixture-account"});
  static DownloadJobs engine({"/series/unused", "/series/unused.tmp", "/series/unused.bak"}, admission,
                             {&state, [](void*) { return DownloadJobs::Headroom + 10000; },
                              [](void* p, const DownloadSpec&) {
                                ++static_cast<State*>(p)->links;
                                return true;
                              }});
  static SeriesQueue queue(store, engine, source,
                           {&state, [](void* p) { return static_cast<State*>(p)->writable; },
                            [](void* p) { return static_cast<State*>(p)->online; },
                            [](void* p, const storage::SeriesQueueRecord&) { return static_cast<State*>(p)->current; }},
                           {"fixture-server", "fixture-account"}, {7, 1}, "/series/job");
  static storage::SeriesQueueRecord review;
  static SeriesQueueView view;
  admission.setReading(false);
  bool ok = (Storage.mkdir("/series") || Storage.exists("/series")) && queue.recover();
  const bool recovering = !std::strcmp(mode, "recover") || !std::strcmp(mode, "resume");
  uint64_t id = queue.find("synthetic-series");
  if (!recovering) {
    review.clear();
    review.token = 42;
    review.count = 3;
    std::strcpy(review.request.key.data(), "synthetic-series");
    constexpr uint8_t digest[] = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
                                  0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
                                  0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
    for (unsigned i = 0; i < 3; ++i) {
      auto& row = review.books[i];
      row.key.ordinal = i;
      row.file.identity.book.bytes[0] = i + 1;
      row.file.identity.edition.bytes[0] = i + 1;
      std::memcpy(row.file.identity.revision.bytes.data(), digest, 32);
      std::strcpy(row.file.server.data(), "fixture-server");
      std::strcpy(row.file.account.data(), "fixture-account");
      std::strcpy(row.file.book.data(), "opaque-book");
      std::snprintf(row.file.file.data(), row.file.file.size(), "file-%u", i);
      std::strcpy(row.file.variant.data(), "epub");
      std::snprintf(row.file.path.data(), row.file.path.size(), "/series/book-%u.epub", i);
      std::snprintf(row.title.data(), row.title.size(), "Synthetic %u", i);
      row.file.bytes = 3;
      row.file.sizeKnown = true;
    }
    if (!std::strcmp(mode, "unsupported")) review.books[1].state = SeriesItemState::Unsupported;
    if (!std::strcmp(mode, "stale")) state.current = false;
    id = queue.start(review);
    if (!std::strcmp(mode, "stale")) {
      ok = ok && !id && !source.opens;
    } else
      ok = ok && id;
    if (!std::strcmp(mode, "cancel")) ok = ok && queue.cancel(id, 1);
    if (!std::strcmp(mode, "failure")) source.bad = true;
  } else if (!std::strcmp(mode, "resume"))
    ok = ok && queue.resume(id);
  for (unsigned tick = 0; ok && tick < 200; ++tick) {
    queue.tick({7, 1});
    if (!id) break;
    ok = queue.read(id, view) && ok;
    if (!std::strcmp(mode, "offline") && view.items[0].state == SeriesItemState::Complete) state.online = false;
    if (!std::strcmp(mode, "cut") && view.items[0].state == SeriesItemState::Complete && engine.busy()) {
      LOG_INF("BSQ", "BookSync Series queue intentional power cut");
      std::fflush(nullptr);
      std::_Exit(73);
    }
  }
  if (id) ok = queue.read(id, view) && ok;
  if (!std::strcmp(mode, "recover"))
    ok = ok && view.paused && source.opens == 0 && view.items[0].state == SeriesItemState::Complete;
  else if (!std::strcmp(mode, "resume"))
    ok = ok && view.complete && source.opens == 2;
  else if (!std::strcmp(mode, "offline"))
    ok = ok && view.paused && source.opens == 1;
  else if (!std::strcmp(mode, "failure"))
    ok = ok && view.paused && source.opens == 3;
  else if (std::strcmp(mode, "stale"))
    ok = ok && view.complete &&
         source.opens == unsigned(!std::strcmp(mode, "cancel") || !std::strcmp(mode, "unsupported") ? 2 : 3);
  LOG_INF("BSQ", "BookSync Series queue %s: %s; opens=%u; workspace=%u; completed=%u", ok ? "PASS" : "FAIL", mode,
          source.opens, unsigned(sizeof(queue)), unsigned(view.complete));
  std::fflush(nullptr);
  std::_Exit(ok ? 0 : 1);
}
