// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>

#include "booksync/import/Credentials.h"
#include "booksync/server/bookorbit/Auth.h"
inline void runCredentialsScenario() {
  const char* boot = std::getenv("BOOKSYNC_CREDENTIALS_SCENARIO");
  if (!boot) return;
  using namespace booksync;
  using namespace booksync::legacy;
  constexpr uint8_t key[6]{2, 0, 0, 0, 0, 1};  // Public F03 fake, never device material.
  Credentials importer(key);
  struct OfflineTransport final : server::AuthTransport {
    unsigned network = 0, restored = 0;
    AuthCapabilities capabilities() const override { return {true, true, true, true}; }
    server::AuthReply authenticate(LoginKind, std::string_view, std::string_view, std::string_view, bool) override {
      ++network;
      return {};
    }
    server::AuthReply restoreLogin(LoginKind kind, std::string_view origin, std::string_view user,
                                   std::string_view secret, bool derived) override {
      if (origin != "synthetic-origin" || user.empty() || secret.empty() || kind == LoginKind::Account ||
          (kind == LoginKind::Sync && (!derived || secret.size() != 32)))
        return {};
      ++restored;
      return {200, false, 7};
    }
    server::AuthReply restoreAuthorizedLogin(LoginKind kind, std::string_view origin, std::string_view user,
                                             std::string_view secret, bool derived, std::string_view identity,
                                             std::string_view authorization) override {
      if (identity != "synthetic-access" || authorization != "synthetic-access-secret") return {};
      return restoreLogin(kind, origin, user, secret, derived);
    }
    void finishAuth(bool) override {}
    void clearAuth() override {}
  } transport;
  server::Auth adapter(transport);
  Account account(adapter);
  Account nativeAccount(adapter);
  Account protectedAccount(adapter);
  struct Networks final : WifiTarget {
    unsigned count = 0;
    bool committed = false;
    bool begin(size_t n, std::string_view last) override { return n == 1 && last == "synthetic-network"; }
    bool add(std::string_view ssid, std::string_view password) override {
      ++count;
      return ssid == "synthetic-network" && password == "synthetic-wifi-secret";
    }
    bool finish(bool accept) override {
      committed = accept;
      return accept;
    }
  } wifi;
  const auto expected = *boot == '0' ? ImportResult::Imported : ImportResult::AlreadyImported;
  bool ok = true;
  unsigned checks = 0;
  auto check = [&](bool value) {
    ++checks;
    ok = value && ok;
  };
  const storage::PublishPaths sync{"/.booksync/import/sync", "/.booksync/import/sync.tmp",
                                   "/.booksync/import/sync.bak"};
  const storage::PublishPaths acquisition{"/.booksync/import/acquisition", "/.booksync/import/acquisition.tmp",
                                          "/.booksync/import/acquisition.bak"};
  const storage::PublishPaths network{"/.booksync/import/networks", "/.booksync/import/networks.tmp",
                                      "/.booksync/import/networks.bak"};
  check(importer.stage(CredentialFamily::LibrarySync, "/legacy/library.json", sync) == expected);
  check(importer.restore(CredentialFamily::LibrarySync, sync, account) == ImportResult::Restored);
  check(importer.stage(CredentialFamily::Acquisition, "/legacy/acquisition.json", acquisition) == expected);
  check(importer.restore(CredentialFamily::Acquisition, acquisition, account) == ImportResult::Restored);
  check(account.snapshot().syncReady && account.snapshot().acquisitionReady && !account.snapshot().signedIn);
  check(importer.stage(CredentialFamily::Wifi, "/legacy/networks.json", network) == expected);
  check(importer.restoreWifi(network, wifi) == ImportResult::Restored && wifi.committed && wifi.count == 1);
  const storage::PublishPaths bad{"/.booksync/import/bad", "/.booksync/import/bad.tmp", "/.booksync/import/bad.bak"};
  check(importer.stage(CredentialFamily::Wifi, "/legacy/corrupt.json", bad) == ImportResult::Unresolved);
  check(importer.stage(CredentialFamily::Wifi, "/legacy/unknown.json", bad) == ImportResult::Unresolved);
  check(importer.stage(CredentialFamily::Wifi, "/legacy/old.json", bad) == ImportResult::Unresolved);
  const storage::PublishPaths native{"/.booksync/import/native", "/.booksync/import/native.tmp",
                                     "/.booksync/import/native.bak"};
  check(importer.stage(CredentialFamily::NativeSync, "/legacy/native.json", native) == expected);
  check(importer.restore(CredentialFamily::NativeSync, native, nativeAccount) == ImportResult::Restored);
  check(nativeAccount.snapshot().syncReady && !nativeAccount.snapshot().signedIn);
  const storage::PublishPaths protectedPaths{"/.booksync/import/protected", "/.booksync/import/protected.tmp",
                                             "/.booksync/import/protected.bak"};
  check(importer.stage(CredentialFamily::LibrarySync, "/legacy/protected.json", protectedPaths) == expected);
  check(importer.restore(CredentialFamily::LibrarySync, protectedPaths, protectedAccount) == ImportResult::Restored);
  check(protectedAccount.snapshot().syncReady && !protectedAccount.snapshot().signedIn);
  check(transport.network == 0 && transport.restored == 4);
  check(!Storage.exists(bad.current));
  LOG_INF("BSI", "BookSync credentials scenario %s: %u checks; network=%u; workspace-bound=20480", ok ? "PASS" : "FAIL",
          checks, transport.network);
  std::fflush(nullptr);
  std::_Exit(ok && checks == 18 ? 0 : 1);
}
