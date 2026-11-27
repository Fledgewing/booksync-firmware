#include <HalStorage.h>
#include <gtest/gtest.h>

#include <fstream>
#include <iterator>

#include "booksync/import/Credentials.h"
#include "booksync/server/bookorbit/Auth.h"
using namespace booksync;
using namespace booksync::legacy;
namespace {
constexpr uint8_t key[6]{2, 0, 0, 0, 0, 1};  // F03 synthetic hardware key only.
constexpr storage::PublishPaths paths{"/.booksync/credentials", "/.booksync/credentials.tmp",
                                      "/.booksync/credentials.bak"};
std::string fixture(const char* name) {
  std::ifstream in(std::string(FIXTURE_ROOT) + "/sd/.crosspoint/" + name);
  return {std::istreambuf_iterator<char>(in), {}};
}
struct Transport final : server::AuthTransport {
  unsigned network = 0, restores = 0;
  AuthCapabilities capabilities() const override { return {true, true, true}; }
  server::AuthReply authenticate(LoginKind, std::string_view, std::string_view, std::string_view, bool) override {
    ++network;
    return {};
  }
  server::AuthReply restoreLogin(LoginKind kind, std::string_view origin, std::string_view user,
                                 std::string_view secret, bool derived) override {
    ++restores;
    if (origin.empty() || user.empty() || secret.empty() || kind == LoginKind::Account) return {};
    if (kind == LoginKind::Sync && (!derived || secret.size() != 32)) return {};
    return {200, false, 7};
  }
  void finishAuth(bool) override {}
  void clearAuth() override {}
};
struct Wifi final : WifiTarget {
  unsigned count = 0;
  bool committed = false;
  bool begin(size_t n, std::string_view last) override {
    count = 0;
    return n == 1 && last == "synthetic-network";
  }
  bool add(std::string_view ssid, std::string_view secret) override {
    ++count;
    return ssid == "synthetic-network" && secret == "synthetic-wifi-secret";
  }
  bool finish(bool accept) override {
    committed = accept;
    return accept;
  }
};
class CredentialsTest : public testing::Test {
 protected:
  // Reusable worker job workspace, outside the small firmware task stack.
  std::unique_ptr<Credentials> importer{new Credentials(key)};
  void SetUp() override { card = {}; }
  void seed(const std::string& raw) {
    card.files["/legacy"] = raw;
    card.disk = card.files;
  }
};
}  // namespace
TEST_F(CredentialsTest, F03EmptyOriginRetainedAndNeverBecomesWebLogin) {
  seed(fixture("bookorbit.json"));
  ASSERT_EQ(importer->stage(CredentialFamily::LibrarySync, "/legacy", paths), ImportResult::Imported);
  Transport transport;
  server::Auth adapter(transport);
  Account account(adapter);
  EXPECT_EQ(importer->restore(CredentialFamily::LibrarySync, paths, account), ImportResult::NeedsOrigin);
  EXPECT_EQ(transport.network, 0u);
  EXPECT_EQ(transport.restores, 0u);
  EXPECT_FALSE(account.snapshot().signedIn);
  EXPECT_EQ(card.files.at("/legacy"), fixture("bookorbit.json"));
}
TEST_F(CredentialsTest, TwoBootsPreserveSyncAndNeverAuthenticate) {
  auto raw = fixture("bookorbit.json");
  raw.replace(raw.find("\"serverUrl\": \"\""), 15, "\"serverUrl\": \"synthetic-origin\"");
  const auto access = raw.find("  \"cfClientId\"");
  const auto after = raw.find("  \"downloadFolder\"");
  raw.erase(access, after - access);
  seed(raw);
  for (unsigned boot = 0; boot < 2; ++boot) {
    if (boot) card.restart();
    Transport transport;
    server::Auth adapter(transport);
    Account account(adapter);
    EXPECT_EQ(importer->stage(CredentialFamily::LibrarySync, "/legacy", paths),
              boot ? ImportResult::AlreadyImported : ImportResult::Imported);
    EXPECT_EQ(importer->restore(CredentialFamily::LibrarySync, paths, account), ImportResult::Restored);
    EXPECT_TRUE(account.snapshot().syncReady);
    EXPECT_FALSE(account.snapshot().signedIn);
    EXPECT_EQ(transport.network, 0u);
    EXPECT_EQ(transport.restores, 1u);
    EXPECT_EQ(card.files.at("/legacy"), raw);
  }
}
TEST_F(CredentialsTest, WifiTwoBootsAndExactPrivateSnapshot) {
  auto raw = fixture("wifi.json");
  seed(raw);
  for (unsigned boot = 0; boot < 2; ++boot) {
    if (boot) card.restart();
    EXPECT_EQ(importer->stage(CredentialFamily::Wifi, "/legacy", paths),
              boot ? ImportResult::AlreadyImported : ImportResult::Imported);
    Wifi wifi;
    EXPECT_EQ(importer->restoreWifi(paths, wifi), ImportResult::Restored);
    EXPECT_TRUE(wifi.committed);
    EXPECT_EQ(wifi.count, 1u);
    EXPECT_EQ(card.files.at("/legacy"), raw);
  }
}
TEST_F(CredentialsTest, UnknownOldCorruptAndWrongDeviceNeverPublished) {
  for (const auto& raw : {std::string("{\"credentials\":"), std::string("{\"version\":99}"),
                          std::string("{\"credentials\":[{\"ssid\":\"fake\",\"password_obf\":\"b2xk\"}]}"),
                          fixture("wifi.json") + "false", std::string(8193, ' ')}) {
    seed(raw);
    EXPECT_NE(importer->stage(CredentialFamily::Wifi, "/legacy", paths), ImportResult::Imported);
    EXPECT_EQ(card.files.at("/legacy"), raw);
    EXPECT_FALSE(card.files.count(paths.current));
  }
  uint8_t other[6]{3, 0, 0, 0, 0, 1};
  Credentials wrong(other);
  seed(fixture("wifi.json"));
  EXPECT_EQ(wrong.stage(CredentialFamily::Wifi, "/legacy", paths), ImportResult::Unresolved);
}
TEST_F(CredentialsTest, RefusesDestinationConflictAndSourcePathAliasing) {
  seed(fixture("wifi.json"));
  EXPECT_EQ(importer->stage(CredentialFamily::Wifi, "/legacy", {"/legacy", "/a", "/b"}), ImportResult::Unresolved);
  card.files[paths.current] = "unrecognized-owned";
  card.disk = card.files;
  EXPECT_EQ(importer->stage(CredentialFamily::Wifi, "/legacy", paths), ImportResult::Conflict);
  EXPECT_EQ(card.files.at(paths.current), "unrecognized-owned");
}
TEST_F(CredentialsTest, FullReadOnlyAndEveryPublicationCutRetainSource) {
  const auto raw = fixture("wifi.json");
  seed(raw);
  ASSERT_EQ(importer->stage(CredentialFamily::Wifi, "/legacy", paths), ImportResult::Imported);
  const auto steps = card.events.size();
  for (size_t cut = 0; cut < steps; ++cut) {
    seed(raw);
    card.files.erase(paths.current);
    card.files.erase(paths.staging);
    card.files.erase(paths.backup);
    card.disk = card.files;
    card.events.clear();
    card.stop = static_cast<int>(cut);
    try {
      importer->stage(CredentialFamily::Wifi, "/legacy", paths);
    } catch (const PowerCut&) {
    }
    card.restart();
    EXPECT_EQ(card.files.at("/legacy"), raw);
    const auto result = importer->stage(CredentialFamily::Wifi, "/legacy", paths);
    EXPECT_TRUE(result == ImportResult::Imported || result == ImportResult::AlreadyImported);
  }
  for (bool full : {false, true}) {
    card = {};
    seed(raw);
    card.readOnly = !full;
    card.full = full;
    EXPECT_NE(importer->stage(CredentialFamily::Wifi, "/legacy", paths), ImportResult::Imported);
    EXPECT_EQ(card.files.at("/legacy"), raw);
  }
}

TEST_F(CredentialsTest, NativeSyncAndIndependentAcquisitionUseF15WithoutWebAccount) {
  for (const auto family : {CredentialFamily::NativeSync, CredentialFamily::Acquisition}) {
    const bool acquisition = family == CredentialFamily::Acquisition;
    auto raw = fixture(acquisition ? "opds.json" : "koreader.json");
    const std::string empty = acquisition ? "\"url\": \"\"" : "\"serverUrl\": \"\"";
    raw.replace(raw.find(empty), empty.size(),
                acquisition ? "\"url\": \"synthetic-origin\"" : "\"serverUrl\": \"synthetic-origin\"");
    if (!acquisition) {
      const std::string emptyHeaders = "\"headers\": []";
      raw.replace(raw.find(emptyHeaders), emptyHeaders.size(),
                  "\"headers\": [{\"name\":\"\",\"value_obf\":\"\"},{\"name\":\"\",\"value_obf\":\"\"}]");
    }
    card = {};
    seed(raw);
    ASSERT_EQ(importer->stage(family, "/legacy", paths), ImportResult::Imported);
    for (unsigned boot = 0; boot < 2; ++boot) {
      if (boot) card.restart();
      Transport transport;
      server::Auth adapter(transport);
      Account account(adapter);
      EXPECT_EQ(importer->restore(family, paths, account), ImportResult::Restored);
      EXPECT_EQ(transport.network, 0u);
      EXPECT_EQ(transport.restores, 1u);
      EXPECT_EQ(account.snapshot().acquisitionReady, acquisition);
      EXPECT_EQ(account.snapshot().syncReady, !acquisition);
      EXPECT_FALSE(account.snapshot().signedIn);
    }
  }
}
TEST_F(CredentialsTest, UnsupportedAccessProtectionRetainedWithoutUnsafeFallback) {
  auto raw = fixture("bookorbit.json");
  const std::string empty = "\"serverUrl\": \"\"";
  raw.replace(raw.find(empty), empty.size(), "\"serverUrl\": \"synthetic-origin\"");
  seed(raw);
  ASSERT_EQ(importer->stage(CredentialFamily::LibrarySync, "/legacy", paths), ImportResult::Imported);
  Transport transport;
  server::Auth adapter(transport);
  Account account(adapter);
  EXPECT_EQ(importer->restore(CredentialFamily::LibrarySync, paths, account), ImportResult::Unresolved);
  EXPECT_EQ(transport.network, 0u);
  EXPECT_EQ(transport.restores, 0u);
  EXPECT_EQ(card.files.at("/legacy"), raw);
}
TEST_F(CredentialsTest, RestoreDoesNotReplaceUserLoginAndErasesRejectedCapture) {
  Transport transport;
  server::Auth adapter(transport);
  Account account(adapter);
  CredentialCapture input;
  auto capture = [&](LoginKind kind) {
    char fake[] = "obvious-fake-secret";
    EXPECT_TRUE(input.capture(kind, "synthetic-origin", "synthetic-user", fake, sizeof(fake) - 1));
    for (char byte : fake) EXPECT_EQ(byte, 0);
  };
  capture(LoginKind::Sync);
  EXPECT_EQ(account.restoreLogin(input, account.snapshot().ticket), AuthError::None);
  EXPECT_FALSE(input.ready());
  const auto ticket = account.snapshot().ticket;
  capture(LoginKind::Sync);
  EXPECT_EQ(account.restoreLogin(input, ticket), AuthError::Stale);
  EXPECT_FALSE(input.ready());
  EXPECT_EQ(transport.restores, 1u);
  EXPECT_EQ(account.snapshot().ticket.epoch, ticket.epoch);
  capture(LoginKind::Account);
  EXPECT_EQ(account.restoreLogin(input, ticket), AuthError::Stale);
  EXPECT_FALSE(input.ready());
  EXPECT_FALSE(account.snapshot().signedIn);
  EXPECT_EQ(transport.network, 0u);
}
TEST_F(CredentialsTest, ChangedSourceUnknownVersionAndDamagedSnapshotAreRetained) {
  const auto raw = fixture("wifi.json");
  seed(raw);
  ASSERT_EQ(importer->stage(CredentialFamily::Wifi, "/legacy", paths), ImportResult::Imported);
  const auto snapshot = card.files.at(paths.current);
  card.files["/legacy"] = raw + " ";
  card.disk = card.files;
  EXPECT_EQ(importer->stage(CredentialFamily::Wifi, "/legacy", paths), ImportResult::Conflict);
  EXPECT_EQ(card.files.at(paths.current), snapshot);
  card.files[paths.backup] = snapshot;
  card.files[paths.current][0] = 'X';
  const auto damaged = card.files.at(paths.current);
  Wifi target;
  EXPECT_EQ(importer->restoreWifi(paths, target), ImportResult::Restored);
  EXPECT_EQ(card.files.at(paths.current), damaged);
  card.files.erase(paths.backup);
  Wifi rejected;
  EXPECT_EQ(importer->restoreWifi(paths, rejected), ImportResult::Unresolved);
  EXPECT_FALSE(rejected.committed);
  EXPECT_EQ(card.files.at(paths.current), damaged);
}
TEST_F(CredentialsTest, PartialWifiAndUnsupportedSchemaNeverPublish) {
  const auto raw = fixture("wifi.json");
  for (const auto& invalid :
       {std::string("{\"lastConnectedSsid\":\"\",\"credentials\":[{\"ssid\":\"fake\",\"password_obf\":7}]}"),
        std::string("{\"lastConnectedSsid\":\"\",\"credentials\":[{\"ssid\":\"fake\",\"password\":\"obvious-fake\"}]}"),
        std::string("{\"cfgVersion\":2,\"username\":\"fake\"}"),
        std::string("{\"lastConnectedSsid\":\"\",\"credentials\":{},\"version\":99}"), raw.substr(0, raw.size() / 2)}) {
    card = {};
    seed(invalid);
    EXPECT_NE(importer->stage(CredentialFamily::Wifi, "/legacy", paths), ImportResult::Imported);
    EXPECT_EQ(card.files.at("/legacy"), invalid);
    EXPECT_FALSE(card.files.count(paths.current));
  }
}
TEST_F(CredentialsTest, UnknownNativeTypeAndCustomAuthorizationStayUnresolved) {
  auto raw = fixture("koreader.json");
  const std::string origin = "\"serverUrl\": \"\"";
  raw.replace(raw.find(origin), origin.size(), "\"serverUrl\": \"synthetic-origin\"");
  auto future = raw;
  const std::string type = "\"serverType\": 0";
  future.replace(future.find(type), type.size(), "\"serverType\": 99");
  seed(future);
  EXPECT_EQ(importer->stage(CredentialFamily::NativeSync, "/legacy", paths), ImportResult::Unresolved);
  EXPECT_EQ(card.files.at("/legacy"), future);
  const std::string headers = "\"headers\": []";
  raw.replace(raw.find(headers), headers.size(),
              "\"headers\": [{\"name\":\"synthetic-header\",\"value\":\"obvious-fake\"}]");
  card = {};
  seed(raw);
  EXPECT_EQ(importer->stage(CredentialFamily::NativeSync, "/legacy", paths), ImportResult::Imported);
  Transport transport;
  server::Auth adapter(transport);
  Account account(adapter);
  EXPECT_EQ(importer->restore(CredentialFamily::NativeSync, paths, account), ImportResult::Unresolved);
  EXPECT_EQ(transport.network, 0u);
  EXPECT_EQ(transport.restores, 0u);
  EXPECT_EQ(card.files.at("/legacy"), raw);
}
TEST_F(CredentialsTest, FailedWifiTransactionRollsBackAndCanRestoreOnNextBoot) {
  seed(fixture("wifi.json"));
  ASSERT_EQ(importer->stage(CredentialFamily::Wifi, "/legacy", paths), ImportResult::Imported);
  struct Reject final : WifiTarget {
    bool rolledBack = false;
    bool begin(size_t, std::string_view) override { return true; }
    bool add(std::string_view, std::string_view) override { return false; }
    bool finish(bool accept) override {
      rolledBack = !accept;
      return false;
    }
  } target;
  EXPECT_EQ(importer->restoreWifi(paths, target), ImportResult::Unresolved);
  EXPECT_TRUE(target.rolledBack);
  card.restart();
  Wifi next;
  EXPECT_EQ(importer->restoreWifi(paths, next), ImportResult::Restored);
}

TEST_F(CredentialsTest, ProtectedLoginRestoresBothPrivateSecretsAcrossTwoBoots) {
  auto raw = fixture("bookorbit.json");
  const std::string empty = "\"serverUrl\": \"\"";
  raw.replace(raw.find(empty), empty.size(), "\"serverUrl\": \"synthetic-origin\"");
  seed(raw);
  struct Protected final : server::AuthTransport {
    unsigned network = 0, restores = 0;
    AuthCapabilities capabilities() const override { return {true, true, true, true}; }
    server::AuthReply authenticate(LoginKind, std::string_view, std::string_view, std::string_view, bool) override {
      ++network;
      return {};
    }
    server::AuthReply restoreAuthorizedLogin(LoginKind kind, std::string_view origin, std::string_view user,
                                             std::string_view secret, bool derived, std::string_view identity,
                                             std::string_view authorization) override {
      ++restores;
      if (kind != LoginKind::Sync || origin != "synthetic-origin" || user != "synthetic-account" || !derived ||
          secret.size() != 32 || identity != "synthetic-access" || authorization != "synthetic-access-secret")
        return {};
      return {200, false, 9};
    }
    void finishAuth(bool) override {}
    void clearAuth() override {}
  };
  for (unsigned boot = 0; boot < 2; ++boot) {
    if (boot) card.restart();
    EXPECT_EQ(importer->stage(CredentialFamily::LibrarySync, "/legacy", paths),
              boot ? ImportResult::AlreadyImported : ImportResult::Imported);
    Protected transport;
    server::Auth adapter(transport);
    Account account(adapter);
    EXPECT_EQ(importer->restore(CredentialFamily::LibrarySync, paths, account), ImportResult::Restored);
    EXPECT_TRUE(account.snapshot().syncReady);
    EXPECT_FALSE(account.snapshot().signedIn);
    EXPECT_EQ(transport.network, 0u);
    EXPECT_EQ(transport.restores, 1u);
    EXPECT_EQ(card.files.at("/legacy"), raw);
  }
}
