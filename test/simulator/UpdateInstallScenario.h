// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>

#include "../fixtures/update_install/FakeUpdate.h"
inline void runUpdateInstallScenario() {
  if (!std::getenv("BOOKSYNC_UPDATE_INSTALL_SCENARIO")) return;
  update_fixture::FakeUpdate fake;
  unsigned checks = 0;
  bool ok = true;
  update_fixture::scenario(fake, [&](bool value) {
    ++checks;
    ok = ok && value;
  });
  LOG_INF("SMOKE", "BookSync install scenario %s checks=%u", ok ? "PASS" : "FAIL", checks);
  std::fflush(nullptr);
  std::_Exit(ok ? 0 : 1);
}
