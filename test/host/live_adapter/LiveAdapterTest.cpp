#include <gtest/gtest.h>

#include "booksync/server/bookorbit/LiveAdapter.h"
#include "booksync/services/Account.h"

using namespace booksync;
using namespace booksync::server;
namespace {
class Http final : public HttpPort {
 public:
  unsigned calls = 0;
  int status = 200;
  std::string user, key, path, body = R"({"items":[],"hasMore":false,"total":0})";
  size_t offset = 0;
  int open(const HttpLogin& login, const char*, std::string_view route, std::string_view, size_t) override {
    ++calls;
    user = login.user.data();
    key = login.secret.data();
    path = route;
    offset = 0;
    return status;
  }
  int read(char* out, size_t bytes) override {
    const auto count = std::min(bytes, body.size() - offset);
    memcpy(out, body.data() + offset, count);
    offset += count;
    return count;
  }
  int finish() override { return status; }
  void close() override {}
};
class LiveAdapterTest : public testing::Test {
 protected:
  Http http;
  LiveAdapter adapter{http};
  Account first{adapter}, second{adapter};
  CredentialCapture capture;
  AuthError restore(Account& account, const char* user) {
    char key[] = "0123456789abcdef0123456789abcdef";
    EXPECT_TRUE(capture.capture(LoginKind::Sync, "synthetic-origin", user, key, 32, true));
    return account.restoreLogin(capture, account.snapshot().ticket);
  }
};
TEST_F(LiveAdapterTest, RestoreNeverMakesAnAuthenticationRequestAndErasesInput) {
  EXPECT_EQ(restore(first, "first"), AuthError::None);
  EXPECT_EQ(http.calls, 0u);
  EXPECT_FALSE(capture.ready());
  EXPECT_TRUE(first.snapshot().syncReady);
  EXPECT_FALSE(first.snapshot().signedIn);
  EXPECT_TRUE(adapter.selectSyncSession(first.snapshot().syncOwner));
  EXPECT_NE(adapter.catalogSource(), nullptr);
}
TEST_F(LiveAdapterTest, SelectedSessionOwnsCatalogCredentials) {
  ASSERT_EQ(restore(first, "first"), AuthError::None);
  ASSERT_EQ(restore(second, "second"), AuthError::None);
  ASSERT_TRUE(adapter.selectSyncSession(first.snapshot().syncOwner));
  EXPECT_TRUE(adapter.startCatalog("synthetic-device"));
  EXPECT_EQ(http.user, "first");
  EXPECT_EQ(http.key, "0123456789abcdef0123456789abcdef");
  EXPECT_EQ(http.path.find("/koreader/plugin/catalog/manifest?size=200"), 0u);
  ASSERT_TRUE(adapter.selectSyncSession(second.snapshot().syncOwner));
  EXPECT_TRUE(adapter.startCatalog("synthetic-device"));
  EXPECT_EQ(http.user, "second");
}
TEST_F(LiveAdapterTest, RemovingOneConnectorCannotEraseAnother) {
  ASSERT_EQ(restore(first, "first"), AuthError::None);
  ASSERT_EQ(restore(second, "second"), AuthError::None);
  const auto previous = first.snapshot().syncOwner;
  ASSERT_EQ(first.removeLogin(LoginKind::Sync, first.snapshot().ticket), AuthError::None);
  EXPECT_FALSE(adapter.selectSyncSession(previous));
  EXPECT_TRUE(adapter.selectSyncSession(second.snapshot().syncOwner));
  EXPECT_TRUE(adapter.startCatalog("synthetic-device"));
  EXPECT_EQ(http.user, "second");
}
TEST_F(LiveAdapterTest, ExpiredCredentialsCannotBecomeAnEmptySuccessfulCatalog) {
  ASSERT_EQ(restore(first, "first"), AuthError::None);
  ASSERT_TRUE(adapter.selectSyncSession(first.snapshot().syncOwner));
  http.status = 401;
  EXPECT_FALSE(adapter.startCatalog("synthetic-device"));
  EXPECT_EQ(adapter.catalogSource()->error(), CatalogError::PermissionLost);
  EXPECT_EQ(adapter.sessionError(), AuthError::Unauthorized);
}
TEST_F(LiveAdapterTest, OfflineAndUnknownSessionNeverFallBackToAnotherLogin) {
  ASSERT_EQ(restore(first, "first"), AuthError::None);
  ASSERT_TRUE(adapter.selectSyncSession(first.snapshot().syncOwner));
  http.status = 0;
  EXPECT_FALSE(adapter.startCatalog("synthetic-device"));
  EXPECT_FALSE(adapter.selectSyncSession(999));
  EXPECT_EQ(adapter.catalogSource(), nullptr);
  EXPECT_FALSE(adapter.startCatalog("synthetic-device"));
  EXPECT_EQ(http.calls, 1u);
}
}  // namespace
TEST_F(LiveAdapterTest, ConnectorCapacityIsBoundedAndRemovedSlotsCanBeReused) {
  std::array<uint32_t, 10> owners{};
  for (auto& owner : owners) {
    char key[] = "0123456789abcdef0123456789abcdef";
    ASSERT_TRUE(capture.capture(LoginKind::Sync, "synthetic-origin", "synthetic-user", key, 32, true));
    const auto result = adapter.restoreLogin(capture);
    ASSERT_EQ(result.error, AuthError::None);
    owner = result.owner;
  }
  EXPECT_EQ(restore(first, "extra"), AuthError::Unavailable);
  EXPECT_EQ(adapter.removeLogin(LoginKind::Sync, owners[0]), AuthError::None);
  EXPECT_EQ(restore(first, "replacement"), AuthError::None);
  EXPECT_GT(first.snapshot().syncOwner, owners.back());
  EXPECT_FALSE(adapter.selectSyncSession(owners[0]));
  EXPECT_TRUE(adapter.selectSyncSession(first.snapshot().syncOwner));
}
TEST_F(LiveAdapterTest, IndependentLoginSignOutDoesNotEraseItsRetainedConnector) {
  ASSERT_EQ(restore(first, "first"), AuthError::None);
  const auto owner = first.snapshot().syncOwner;
  EXPECT_EQ(first.signOut(first.snapshot().ticket), AuthError::None);
  EXPECT_TRUE(first.snapshot().syncReady);
  EXPECT_TRUE(adapter.selectSyncSession(owner));
}
TEST_F(LiveAdapterTest, HeaderInjectionAndMalformedAdditionalAuthorizationStayUnavailable) {
  EXPECT_EQ(restore(first, "first\r\nInjected: value"), AuthError::Unavailable);
  char key[] = "0123456789abcdef0123456789abcdef";
  ASSERT_TRUE(capture.capture(LoginKind::Sync, "synthetic-origin", "synthetic-user", key, 32, true));
  EXPECT_EQ(adapter.restoreLogin(capture, {"id", {}}).error, AuthError::Unavailable);
  EXPECT_EQ(http.calls, 0u);
}
TEST_F(LiveAdapterTest, WorkerSessionRetirementDropsTheParserButPreservesItsLogin) {
  ASSERT_EQ(restore(first, "first"), AuthError::None);
  const auto owner = first.snapshot().syncOwner;
  ASSERT_TRUE(adapter.selectSyncSession(owner));
  ASSERT_NE(adapter.catalogSource(), nullptr);
  adapter.endSyncSession();
  EXPECT_EQ(adapter.catalogSource(), nullptr);
  EXPECT_FALSE(adapter.startCatalog("synthetic-device"));
  EXPECT_TRUE(adapter.selectSyncSession(owner));
  EXPECT_TRUE(adapter.startCatalog("synthetic-device"));
}
