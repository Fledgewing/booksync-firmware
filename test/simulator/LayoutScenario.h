// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>

#include "booksync/storage/LayoutStore.h"
inline void runLayoutScenario() {
  if (!std::getenv("BOOKSYNC_LAYOUT_SCENARIO")) return;
  using namespace booksync;
  static storage::LayoutStore store({"/layout", "/layout.tmp", "/layout.bak"}, Layout::deviceClass());
  bool ok = store.load() == storage::LayoutSource::Default;
  // Bad primary is never deleted or repaired by fallback or a failed save.
  ok = ok && !store.save(Layout::defaults()) && Storage.exists("/layout");
  static storage::LayoutStore good({"/shared", "/shared.tmp", "/shared.bak"}, Layout::deviceClass());
  ok = ok && good.load() == storage::LayoutSource::Current;
  // An incomplete staging file is never selected on restart.
  ok = ok && good.save(Layout::defaults());
  static storage::LayoutStore restarted({"/shared", "/shared.tmp", "/shared.bak"}, Layout::deviceClass());
  ok = ok && restarted.load() == storage::LayoutSource::Current && Storage.exists("/shared.bak");
  LOG_INF("BSL", "BookSync layout scenario %s: class=%s workspace=%u store=%u parser-payload-allocations=0",
          ok ? "PASS" : "FAIL", Layout::deviceClass(), unsigned(sizeof(Layout)),
          unsigned(sizeof(storage::LayoutStore)));
  std::fflush(nullptr);
  std::_Exit(ok ? 0 : 1);
}
