// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>

#include "../fixtures/version/InstallChoice.h"

inline void runVersionScenario() {
  if (!std::getenv("BOOKSYNC_VERSION_SCENARIO")) return;
  unsigned checks = 0;
  bool ok = true;
  version_fixture::installChain([&](bool value) {
    ++checks;
    ok = value && ok;
  });
  LOG_INF("SMOKE", "BookSync version scenario %s checks=%u", ok ? "PASS" : "FAIL", checks);
  std::fflush(nullptr);
  std::_Exit(ok ? 0 : 1);
}
