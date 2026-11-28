#include <HalStorage.h>
#include <SecureHttpClient.h>
#include <gtest/gtest.h>

#include "booksync/server/bookorbit/LiveAdapter.h"
#include "booksync/system/NetworkPhase.h"

// Use the pinned SDK's scripted socket to fault the production HTTP parser;
// production body spooling, framing and admission checks remain linked.
namespace freeink {
SecureClient::~SecureClient() = default;
void SecureClient::setCACert(const char*) {}
void SecureClient::setInsecure() {}
int SecureClient::connect(IPAddress ip, uint16_t port) { return _transport.connect(ip, port); }
int SecureClient::connect(const char* host, uint16_t port) { return _transport.connect(host, port); }
size_t SecureClient::write(uint8_t b) { return _transport.write(b); }
size_t SecureClient::write(const uint8_t* p, size_t n) { return _transport.write(p, n); }
int SecureClient::available() { return _transport.available(); }
int SecureClient::read() { return _transport.read(); }
int SecureClient::read(uint8_t* p, size_t n) { return _transport.read(p, n); }
int SecureClient::peek() { return _transport.peek(); }
void SecureClient::flush() {}
void SecureClient::stop() { _transport.stop(); }
uint8_t SecureClient::connected() { return _transport.connected(); }
bool SecureClient::tls13Available() { return false; }
bool hostResolves(const char*) { return true; }
}  // namespace freeink
namespace booksync {
MemorySnapshot memorySnapshot() { return {128 * 1024, 64 * 1024, 0}; }
Admission& networkAdmission() {
  static Admission admission({nullptr, [](void*) { return MemorySnapshot{128 * 1024, 64 * 1024, 0}; },
                              [](void*) { return true; }, [](void*) { return true; }, [](void*) { return true; }},
                             false);
  return admission;
}
}  // namespace booksync
using namespace booksync;
using namespace booksync::server;
namespace {
class HttpTest : public testing::Test {
 protected:
  HttpPort& http = deviceHttpPort();
  HttpLogin login;
  void SetUp() override {
    http.close();
    card = {};
    FakeNet::requests().clear();
    FakeNet::replies().clear();
    FakeNet::hosts().clear();
    FakeNet::readDelay() = 0;
    FakeNet::holdOpen() = false;
    fakeNow() = 0;
    strcpy(login.origin.data(), "http://synthetic");
    strcpy(login.user.data(), "synthetic-user");
    strcpy(login.secret.data(), "synthetic-key");
    auto& a = networkAdmission();
    a.setReading(false);
    ASSERT_EQ(a.begin(TAILNET_JOB_CATALOG), AdmissionResult::Admitted);
  }
  void TearDown() override {
    http.close();
    EXPECT_TRUE(networkAdmission().finish());
  }
  void response(std::string body, const char* framing = "") {
    FakeNet::replies().push_back("HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) + "\r\n" +
                                 framing + "\r\n" + body);
  }
};
TEST_F(HttpTest, CompleteBodySpoolsBeforeTheParserCanReadIt) {
  response("complete");
  ASSERT_EQ(http.open(login, "GET", "/catalog", {}, 1024), 200);
  EXPECT_EQ(card.handles, 1);
  char out[16]{};
  EXPECT_EQ(http.read(out, sizeof(out)), 8);
  EXPECT_STREQ(out, "complete");
  EXPECT_EQ(http.finish(), 200);
  EXPECT_NE(FakeNet::requests().front().find("GET /api/v1/catalog HTTP/1.1"), std::string::npos);
  EXPECT_NE(FakeNet::requests().front().find("x-auth-user: synthetic-user"), std::string::npos);
}
TEST_F(HttpTest, MidBodyDisconnectNeverExposesPartialSuccess) {
  FakeNet::replies().push_back("HTTP/1.1 200 OK\r\nContent-Length: 12\r\n\r\nshort");
  EXPECT_EQ(http.open(login, "GET", "/catalog", {}, 1024), 0);
  char out[16];
  EXPECT_EQ(http.read(out, sizeof(out)), -1);
  EXPECT_EQ(http.finish(), 0);
  EXPECT_EQ(card.handles, 0);
}
TEST_F(HttpTest, BoundAndFullCardFailClosed) {
  response("oversized");
  EXPECT_EQ(http.open(login, "GET", "/catalog", {}, 4), 0);
  response("complete");
  card.full = true;
  EXPECT_EQ(http.open(login, "GET", "/catalog", {}, 1024), 0);
  EXPECT_EQ(http.finish(), 0);
}
TEST_F(HttpTest, ReadingRevokesTransportWithoutAnyRequest) {
  response("complete");
  networkAdmission().setReading(true);
  EXPECT_EQ(http.open(login, "GET", "/catalog", {}, 1024), 0);
  EXPECT_TRUE(FakeNet::requests().empty());
  networkAdmission().setReading(false);
}
TEST_F(HttpTest, RedirectCannotForwardPrivateCredentials) {
  FakeNet::replies().push_back("HTTP/1.1 302 Found\r\nLocation: http://other/catalog\r\nContent-Length: 0\r\n\r\n");
  EXPECT_EQ(http.open(login, "GET", "/catalog", {}, 1024), 302);
  EXPECT_EQ(FakeNet::requests().size(), 1u);
  EXPECT_EQ(FakeNet::hosts().front(), "synthetic");
}
TEST_F(HttpTest, ChunkTerminatorAndLengthMustBothBeComplete) {
  FakeNet::replies().push_back("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n0\r\n\r\n");
  EXPECT_EQ(http.open(login, "GET", "/catalog", {}, 1024), 200);
  char out[8]{};
  EXPECT_EQ(http.read(out, sizeof(out)), 3);
  EXPECT_STREQ(out, "abc");
  EXPECT_EQ(http.finish(), 200);
  FakeNet::replies().push_back("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n");
  EXPECT_EQ(http.open(login, "GET", "/catalog", {}, 1024), 0);
}
}  // namespace
TEST_F(HttpTest, ImportedApiSuffixesNormalizeWithoutDuplicatingThePrefix) {
  for (const auto* suffix : {"", "/api/v1", "/api/v1/koreader/"}) {
    const std::string origin = std::string("http://synthetic/prefix") + suffix;
    strcpy(login.origin.data(), origin.c_str());
    response("ok");
    ASSERT_EQ(http.open(login, "GET", "/catalog", {}, 1024), 200);
    EXPECT_NE(FakeNet::requests().back().find("GET /prefix/api/v1/catalog HTTP/1.1"), std::string::npos);
  }
}
TEST_F(HttpTest, ExtraAuthorizationNeverFallsBackToPlaintextOrBasicAuth) {
  strcpy(login.authorizationUser.data(), "synthetic-access-id");
  strcpy(login.authorizationSecret.data(), "synthetic-access-secret");
  response("ok");
  EXPECT_EQ(http.open(login, "GET", "/catalog", {}, 1024), 0);
  EXPECT_TRUE(FakeNet::requests().empty());
  strcpy(login.origin.data(), "https://synthetic");
  ASSERT_EQ(http.open(login, "GET", "/catalog", {}, 1024), 200);
  const auto& request = FakeNet::requests().back();
  EXPECT_NE(request.find("CF-Access-Client-Id: synthetic-access-id"), std::string::npos);
  EXPECT_NE(request.find("CF-Access-Client-Secret: synthetic-access-secret"), std::string::npos);
  EXPECT_EQ(request.find("Authorization: Basic"), std::string::npos);
}
TEST_F(HttpTest, HeaderOrOriginInjectionCannotStartARequest) {
  response("ok");
  EXPECT_EQ(http.open(login, "GET", "/catalog\r\ninvalid", {}, 1024), 0);
  strcpy(login.origin.data(), "http://synthetic?other");
  EXPECT_EQ(http.open(login, "GET", "/catalog", {}, 1024), 0);
  EXPECT_TRUE(FakeNet::requests().empty());
}
TEST_F(HttpTest, UnauthorizedBodyRetainsItsStatusAndCannotBecomeAnEmptySuccess) {
  FakeNet::replies().push_back("HTTP/1.1 401 Unauthorized\r\nContent-Length: 2\r\n\r\n{}");
  EXPECT_EQ(http.open(login, "GET", "/catalog", {}, 1024), 401);
  char out[8];
  EXPECT_EQ(http.read(out, sizeof(out)), 2);
  EXPECT_EQ(http.finish(), 401);
}
