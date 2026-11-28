// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "booksync/services/Layout.h"
#include "booksync/services/LocalLibrary.h"
#include "booksync/services/OfflineLibrary.h"
#include "booksync/services/PersonalWorker.h"
#include "booksync/storage/Index.h"
inline void runNativePersonalScenario() {
  const char* mode = std::getenv("BOOKSYNC_NATIVE_PERSONAL_SCENARIO");
  if (!mode) return;
  using namespace booksync;
  static storage::Index index{{"/.booksync/device.index", "/.booksync/device.index.tmp", "/.booksync/device.index.bak"},
                              "/.booksync/device.a",
                              "/.booksync/device.b"};
  bool okay = Storage.ensureDirectoryExists("/.booksync") && index.begin({7, 1, 0, 9});
  static storage::IndexRecord book;
  std::snprintf(book.path.data(), book.path.size(), "/books/one.epub");
  std::snprintf(book.title.data(), book.title.size(), "Saved book");
  okay = okay && index.append(book) && index.commit();
  enterLocalLibrary();
  publishClockStatus(std::strcmp(mode, "unknown-clock") == 0
                         ? ClockStatus{}
                         : ClockStatus{ClockEvidence::Synchronized, 1795789920, 0, {2026, 11, 27, 5, 14, 32, 0}});
  static WindowBatchRequest request;
  request.count = 1;
  request.windows[0] = {0, 1, LibraryBytes, WindowSource::OnDevice};
  request.personalWidgets = 1;
  // X3 uses the same pure source in isolation; product Home remains unbound.
  static PersonalWidgetBatch result;
  if (std::string_view(Layout::deviceClass()) == "x3") {
    static PersonalWorker worker(index);
    okay = okay && worker.begin(1, 0, offlineLibrary().clockStatus());
    for (unsigned i = 0; okay && worker.busy() && i < 100; ++i) okay = worker.tick();
    okay = okay && !worker.busy();
    result = worker.result();
    okay = okay && !offlineLibrary().requestBatch(request);
  } else {
    okay = okay && offlineLibrary().requestBatch(request);
    for (unsigned i = 0; okay && i < 100; ++i) workLocalLibrary();
    offlineLibrary().readPersonalWidgets(result);
    static LibraryWindow sibling;
    offlineLibrary().readWindow(sibling);
    okay = okay && sibling.count == 1 && sibling.windows[0].availability == Availability::Ready;
  }
  const auto expected = !std::strcmp(mode, "ready") || !std::strcmp(mode, "backup")   ? PersonalCardState::Ready
                        : !std::strcmp(mode, "empty") || !std::strcmp(mode, "scoped") ? PersonalCardState::Empty
                        : !std::strcmp(mode, "oversize")                              ? PersonalCardState::Oversize
                        : !std::strcmp(mode, "bad-header")                            ? PersonalCardState::Unavailable
                                                                                      : PersonalCardState::NotSaved;
  okay = okay && result.requested == 1 && result.highlight.status.state == expected;
  if (expected == PersonalCardState::Ready)
    okay = okay && result.highlight.status.total == 1 && result.highlight.canOpen &&
           result.highlight.status.origin == PersonalOrigin::ThisReader &&
           !std::strcmp(result.highlight.passage.data(), "An original saved passage.");
  leaveLocalLibrary();
  static PersonalWidgetBatch retired;
  offlineLibrary().readPersonalWidgets(retired);
  okay = okay && retired.publication == 0 && retired.highlight.passage[0] == 0;
  if (!okay) {
    LOG_ERR("BSP", "Native personal scenario FAIL (%s)", mode);
    std::_Exit(2);
  }
  LOG_INF("BSP", "BookSync native personal PASS: %s; index-cap=80000 bytes; source=original-capture", mode);
  std::fflush(nullptr);
  std::_Exit(0);
}
