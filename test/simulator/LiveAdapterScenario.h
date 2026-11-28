// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <HalStorage.h>
#include <Logging.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "booksync/services/Account.h"
#include "booksync/services/CatalogSource.h"
#include "booksync/services/ServerAdapter.h"
#include "booksync/system/NetworkPhase.h"
#include "booksync/system/Worker.h"

inline void runLiveAdapterScenario() {
  const char* mode = std::getenv("BOOKSYNC_LIVE_ADAPTER_SCENARIO");
  if (!mode) return;
  using namespace booksync;
  static auto& adapter = defaultServerAdapter();
  static Account account(adapter);
  static CredentialCapture capture;
  static CatalogRow row;
  static unsigned count = 0;
  static bool ready = false, permissionLost = false;
  const char* origin = std::getenv("BOOKSYNC_LIVE_ORIGIN");
  const char* user = std::getenv("BOOKSYNC_LIVE_USER");
  const char* key = std::getenv("BOOKSYNC_LIVE_KEY");
  bool ok = origin && user && key && std::strlen(key) == 32;
  char secret[32]{};
  if (ok) memcpy(secret, key, sizeof(secret));
  ok = ok && capture.capture(LoginKind::Sync, origin, user, secret, sizeof(secret), true);
  ok = ok && account.restoreLogin(capture, account.snapshot().ticket) == AuthError::None;
  ok = ok && (Storage.mkdir("/.booksync") || Storage.exists("/.booksync"));
  auto& admission = networkAdmission();
  admission.setReading(false);
  ok = ok && admission.begin(TAILNET_JOB_CATALOG) == AdmissionResult::Admitted;
  if (ok)
    ok = startNetworkWorker(
        [](void*) {
          if (!adapter.selectSyncSession(account.snapshot().syncOwner)) return;
          if (adapter.startCatalog("synthetic-device")) {
            auto* source = adapter.catalogSource();
            for (unsigned steps = 0; steps < 10000 && networkAdmission().permitted(); ++steps) {
              const auto next = source->next(row);
              if (next == CatalogStep::End) {
                ready = true;
                break;
              }
              if (next == CatalogStep::Error) break;
              ++count;
            }
          }
          permissionLost =
              adapter.sessionError() == AuthError::Unauthorized || adapter.sessionError() == AuthError::Forbidden;
          adapter.endSyncSession();
        },
        nullptr);
  while (ok && !pollNetworkWorker()) std::this_thread::yield();
  ok = admission.finish() && ok;
  if (std::strcmp(mode, "success") == 0)
    ok = ok && ready && count > 0;
  else if (std::strcmp(mode, "auth") == 0)
    ok = ok && !ready && permissionLost && count == 0;
  else
    ok = ok && !ready && count == 0;
  LOG_INF("BSC", "BookSync live adapter scenario %s; rows=%u; network=worker-only", ok ? "PASS" : "FAIL", count);
  std::fflush(nullptr);
  std::_Exit(ok ? 0 : 1);
}
