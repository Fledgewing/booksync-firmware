// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include "booksync/server/bookorbit/Auth.h"
#include "booksync/services/Account.h"
#include "booksync/services/Delivery.h"

using namespace booksync;
namespace {
class FakeAuth final : public AuthAdapter {
 public:
  AuthError result = AuthError::None;
  unsigned calls = 0, clears = 0;
  uint32_t owner = 1;
  bool signedIn = false;
  AuthCapabilities authCapabilities() const override { return {true, true, true}; }
  AuthResult authenticate(const CredentialCapture& input) override {
    ++calls;
    if (result != AuthError::None) return {result};
    if (input.kind() == LoginKind::Account) signedIn = true;
    return {AuthError::None, owner};
  }
  AuthResult useAccountLogin(LoginKind) override { return {result, owner}; }
  AuthError removeLogin(LoginKind) override { return result; }
  void clearAuth() override {
    signedIn = false;
    ++clears;
  }
};
bool emptyBytes(const char* p, size_t size) {
  for (size_t i = 0; i < size; ++i)
    if (p[i]) return false;
  return true;
}
bool capture(CredentialCapture& c, LoginKind kind = LoginKind::Account, bool derived = false) {
  char input[12];
  for (auto& byte : input)
    byte = static_cast<char>(kind == LoginKind::Account ? 65 : kind == LoginKind::Sync ? 66 : 67);
  const bool ok = c.capture(kind, "origin-a", "synthetic-user", input, sizeof(input), derived);
  EXPECT_TRUE(emptyBytes(input, sizeof(input)));
  return ok;
}
}  // namespace
TEST(AccountCapture, WipesSourceAndCannotCopy) {
  static_assert(!std::is_copy_constructible_v<CredentialCapture>);
  static_assert(!std::is_move_constructible_v<CredentialCapture>);
  CredentialCapture c;
  ASSERT_TRUE(capture(c));
  EXPECT_EQ(c.secret().size(), 12u);
  c.clear();
  EXPECT_FALSE(c.ready());
  EXPECT_TRUE(emptyBytes(c.secret().data(), CredentialCapture::SecretCapacity));
}
TEST(AccountCapture, InvalidAndOversizeErasePreviousAndSource) {
  CredentialCapture c;
  ASSERT_TRUE(capture(c));
  char input[CredentialCapture::SecretCapacity + 1];
  for (auto& b : input) b = static_cast<char>(66);
  EXPECT_FALSE(c.capture(LoginKind::Sync, "origin-a", "synthetic-user", input, sizeof(input)));
  EXPECT_TRUE(emptyBytes(input, sizeof(input)));
  EXPECT_FALSE(c.ready());
  EXPECT_TRUE(emptyBytes(c.secret().data(), CredentialCapture::SecretCapacity));
  EXPECT_FALSE(c.capture(LoginKind::Account, "origin-a", "synthetic-user", nullptr, 1));
}
TEST(AccountWorker, EpochsRejectStaleAndABA) {
  FakeAuth adapter;
  Account account(adapter);
  CredentialCapture c;
  const auto initial = account.snapshot().ticket;
  ASSERT_TRUE(capture(c));
  EXPECT_EQ(account.signIn(c, initial), AuthError::None);
  auto first = account.snapshot().ticket;
  EXPECT_NE(first.epoch, initial.epoch);
  EXPECT_TRUE(account.snapshot().signedIn);
  ASSERT_TRUE(capture(c));
  EXPECT_EQ(account.signIn(c, initial), AuthError::Stale);
  EXPECT_FALSE(c.ready());
  EXPECT_EQ(adapter.calls, 1u);
  EXPECT_EQ(account.signOut(first), AuthError::None);
  EXPECT_FALSE(account.snapshot().signedIn);
  ASSERT_TRUE(capture(c));
  EXPECT_EQ(account.signIn(c, account.snapshot().ticket), AuthError::None);
  EXPECT_NE(account.snapshot().ticket.epoch, first.epoch);
  EXPECT_EQ(account.snapshot().ticket.owner, first.owner);
  EXPECT_EQ(account.signOut(first), AuthError::Stale);
  EXPECT_TRUE(account.snapshot().signedIn);
}
TEST(AccountWorker, FailureClearsInputAndPreservesPreviousOwner) {
  FakeAuth adapter;
  Account account(adapter);
  CredentialCapture c;
  ASSERT_TRUE(capture(c));
  ASSERT_EQ(account.signIn(c, account.snapshot().ticket), AuthError::None);
  const auto before = account.snapshot().ticket;
  adapter.result = AuthError::Unauthorized;
  ASSERT_TRUE(capture(c));
  EXPECT_EQ(account.signIn(c, before), AuthError::Unauthorized);
  EXPECT_TRUE(emptyBytes(c.secret().data(), CredentialCapture::SecretCapacity));
  EXPECT_EQ(account.snapshot().ticket.epoch, before.epoch);
  EXPECT_TRUE(account.snapshot().signedIn);
  EXPECT_EQ(account.signOut(before), AuthError::None);
  EXPECT_EQ(adapter.clears, 1u);
}
TEST(AccountWorker, ConnectorDoesNotSignInAndUnsupportedIsTyped) {
  FakeAuth adapter;
  Account account(adapter);
  CredentialCapture c;
  ASSERT_TRUE(capture(c, LoginKind::Sync));
  EXPECT_EQ(account.signIn(c, account.snapshot().ticket), AuthError::None);
  EXPECT_FALSE(account.snapshot().signedIn);
  EXPECT_TRUE(account.snapshot().syncReady);
  EXPECT_FALSE(account.snapshot().acquisitionReady);
  EXPECT_EQ(account.signOut(account.snapshot().ticket), AuthError::None);
  EXPECT_TRUE(account.snapshot().syncReady);
  EXPECT_EQ(account.removeLogin(LoginKind::Sync, account.snapshot().ticket), AuthError::None);
  EXPECT_FALSE(account.snapshot().syncReady);
  AuthAdapter unsupported;
  Account unavailable(unsupported);
  ASSERT_TRUE(capture(c));
  EXPECT_EQ(unavailable.signIn(c, unavailable.snapshot().ticket), AuthError::Unsupported);
  EXPECT_FALSE(c.ready());
}
TEST(AdapterAuth, NormalizesErrorsWithoutClaimingAttestation) {
  using booksync::server::normalizeAuthReply;
  EXPECT_EQ(normalizeAuthReply(401, false), AuthError::Unauthorized);
  EXPECT_EQ(normalizeAuthReply(403, false), AuthError::Forbidden);
  EXPECT_EQ(normalizeAuthReply(403, true), AuthError::AttestationRequired);
  EXPECT_EQ(normalizeAuthReply(200, true), AuthError::AttestationRequired);
  EXPECT_EQ(normalizeAuthReply(404, false), AuthError::TooOld);
  EXPECT_EQ(normalizeAuthReply(426, false), AuthError::TooOld);
  EXPECT_EQ(normalizeAuthReply(429, false), AuthError::Retryable);
  EXPECT_EQ(normalizeAuthReply(0, false), AuthError::Unavailable);
  EXPECT_EQ(normalizeAuthReply(500, false), AuthError::Unavailable);
}
TEST(AdapterAuth, LegacySyncIsIndependentFromWebPassword) {
  using namespace booksync::server;
  struct Transport final : AuthTransport {
    AuthCapabilities capabilities() const override { return {true, true, true}; }
    void finishAuth(bool) override {}
    unsigned calls = 0;
    LoginKind kind = LoginKind::Account;
    bool derived = false;
    AuthReply reply{200, false, 7};
    AuthReply authenticate(LoginKind k, std::string_view, std::string_view, std::string_view, bool d) override {
      ++calls;
      kind = k;
      derived = d;
      return reply;
    }
    void clearAuth() override {}
  } transport;
  Auth adapter(transport);
  CredentialCapture c;
  ASSERT_TRUE(capture(c, LoginKind::Sync, true));
  // Non-key input must fail locally, never be treated as the web password.
  EXPECT_EQ(adapter.authenticate(c).error, AuthError::Invalid);
  EXPECT_EQ(transport.calls, 0u);
  ASSERT_TRUE(capture(c, LoginKind::Sync));
  EXPECT_EQ(adapter.authenticate(c).error, AuthError::None);
  EXPECT_EQ(transport.kind, LoginKind::Sync);
  EXPECT_TRUE(transport.derived);
  ASSERT_TRUE(capture(c));
  transport.reply.attestationRequired = true;
  EXPECT_EQ(adapter.authenticate(c).error, AuthError::AttestationRequired);
  EXPECT_EQ(transport.calls, 2u);
}
TEST(AdapterAuth, MalformedSuccessDoesNotBecomeSignedIn) {
  using namespace booksync::server;
  struct Transport final : AuthTransport {
    AuthCapabilities capabilities() const override { return {true, true, true}; }
    void finishAuth(bool) override {}
    AuthReply authenticate(LoginKind, std::string_view, std::string_view, std::string_view, bool) override {
      return {200, false, 0};
    }
    void clearAuth() override {}
  } transport;
  Auth adapter(transport);
  Account account(adapter);
  CredentialCapture c;
  ASSERT_TRUE(capture(c));
  EXPECT_EQ(account.signIn(c, account.snapshot().ticket), AuthError::InvalidResponse);
  EXPECT_FALSE(account.snapshot().signedIn);
}

TEST(AccountWorker, SameOwnerReauthenticationRetiresTickets) {
  FakeAuth adapter;
  Account account(adapter);
  CredentialCapture c;
  ASSERT_TRUE(capture(c));
  ASSERT_EQ(account.signIn(c, account.snapshot().ticket), AuthError::None);
  const auto first = account.snapshot().ticket;
  ASSERT_TRUE(capture(c));
  EXPECT_EQ(account.signIn(c, first), AuthError::None);
  EXPECT_EQ(first.owner, account.snapshot().ticket.owner);
  EXPECT_NE(first.epoch, account.snapshot().ticket.epoch);
}
TEST(AccountWorker, IndependentOwnersAndAccountSignOut) {
  FakeAuth adapter;
  Account account(adapter);
  CredentialCapture c;
  ASSERT_TRUE(capture(c));
  ASSERT_EQ(account.signIn(c, account.snapshot().ticket), AuthError::None);
  adapter.owner = 2;
  ASSERT_TRUE(capture(c, LoginKind::Sync));
  ASSERT_EQ(account.signIn(c, account.snapshot().ticket), AuthError::None);
  adapter.owner = 3;
  ASSERT_TRUE(capture(c, LoginKind::Acquisition));
  ASSERT_EQ(account.signIn(c, account.snapshot().ticket), AuthError::None);
  auto state = account.snapshot();
  EXPECT_EQ(state.ticket.owner, 3u);
  EXPECT_EQ(state.accountOwner, 1u);
  EXPECT_EQ(state.syncOwner, 2u);
  EXPECT_EQ(state.acquisitionOwner, 3u);
  ASSERT_EQ(account.signOut(state.ticket), AuthError::None);
  state = account.snapshot();
  EXPECT_FALSE(state.signedIn);
  EXPECT_TRUE(state.syncReady && state.acquisitionReady);
  EXPECT_EQ(state.accountOwner, 0u);
}
TEST(AccountCapture, RejectsWrongDerivedKindAndEmbeddedNull) {
  CredentialCapture c;
  char input[2]{65, 65};
  EXPECT_FALSE(c.capture(LoginKind::Account, "origin-a", "synthetic-user", input, sizeof(input), true));
  EXPECT_TRUE(emptyBytes(input, sizeof(input)));
  input[0] = 65;
  EXPECT_FALSE(c.capture(LoginKind::Sync, std::string_view("a\0b", 3), "synthetic-user", input, sizeof(input)));
  EXPECT_TRUE(emptyBytes(input, sizeof(input)));
}
TEST(AdapterAuth, SyncDerivationAndCandidateCommitArePrivate) {
  using namespace booksync::server;
  struct Transport final : AuthTransport {
    bool isKey = false, committed = false, matchesFixture = false;
    unsigned finishCalls = 0, clearCalls = 0;
    bool attest = false;
    AuthCapabilities capabilities() const override { return {true, true, true}; }
    AuthReply authenticate(LoginKind kind, std::string_view, std::string_view, std::string_view secret,
                           bool derived) override {
      isKey = kind == LoginKind::Sync && derived && secret.size() == 32;
      // Compare booleans only: gtest must never format either secret/key value.
      for (char byte : secret) isKey = isKey && ((byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f'));
      matchesFixture = secret == std::string_view("9f296b094e9f7ea769d73718147c9a0a");
      return {200, attest, 7};
    }
    void finishAuth(bool accepted) override {
      committed = accepted;
      ++finishCalls;
    }
    void clearAuth() override {
      committed = false;
      ++clearCalls;
    }
  } transport;
  Auth adapter(transport);
  Account account(adapter);
  CredentialCapture c;
  ASSERT_TRUE(capture(c, LoginKind::Sync));
  ASSERT_EQ(account.signIn(c, account.snapshot().ticket), AuthError::None);
  EXPECT_TRUE(transport.isKey && transport.committed && transport.matchesFixture);
  transport.attest = true;
  ASSERT_TRUE(capture(c));
  EXPECT_EQ(account.signIn(c, account.snapshot().ticket), AuthError::AttestationRequired);
  EXPECT_FALSE(transport.committed);
  EXPECT_EQ(transport.finishCalls, 2u);
  EXPECT_EQ(account.signOut(account.snapshot().ticket), AuthError::None);
  EXPECT_EQ(transport.clearCalls, 1u);
}

TEST(AdapterAuth, ExplicitDerivedKeyIsValidatedAndNotRehashed) {
  using namespace booksync::server;
  struct Transport final : AuthTransport {
    bool normalized = false;
    AuthCapabilities capabilities() const override { return {true, true, true}; }
    AuthReply authenticate(LoginKind kind, std::string_view, std::string_view, std::string_view value,
                           bool derived) override {
      normalized = kind == LoginKind::Sync && derived && value.size() == 32;
      for (char byte : value) normalized = normalized && byte == static_cast<char>(97);
      return {200, false, 5};
    }
    void finishAuth(bool) override {}
    void clearAuth() override {}
  } transport;
  Auth adapter(transport);
  Account account(adapter);
  CredentialCapture c;
  char input[32];
  for (auto& byte : input) byte = static_cast<char>(65);
  ASSERT_TRUE(c.capture(LoginKind::Sync, "origin-a", "synthetic-user", input, sizeof(input), true));
  EXPECT_TRUE(emptyBytes(input, sizeof(input)));
  EXPECT_EQ(account.signIn(c, account.snapshot().ticket), AuthError::None);
  EXPECT_TRUE(transport.normalized);
  EXPECT_TRUE(emptyBytes(c.secret().data(), CredentialCapture::SecretCapacity));
}

TEST(AccountOrigins, SignOutKeepsUserLoginsAndRemovingOneDoesNotClearTheOther) {
  FakeAuth adapter;
  Account account(adapter);
  CredentialCapture c;
  ASSERT_TRUE(capture(c, LoginKind::Sync));
  ASSERT_EQ(account.signIn(c, account.snapshot().ticket), AuthError::None);
  ASSERT_TRUE(capture(c, LoginKind::Acquisition));
  ASSERT_EQ(account.signIn(c, account.snapshot().ticket), AuthError::None);
  ASSERT_TRUE(capture(c));
  ASSERT_EQ(account.signIn(c, account.snapshot().ticket), AuthError::None);
  ASSERT_EQ(account.signOut(account.snapshot().ticket), AuthError::None);
  EXPECT_TRUE(account.snapshot().syncReady && account.snapshot().acquisitionReady);
  EXPECT_EQ(account.snapshot().syncOrigin, LoginOrigin::User);
  EXPECT_EQ(account.removeLogin(LoginKind::Sync, account.snapshot().ticket), AuthError::None);
  EXPECT_FALSE(account.snapshot().syncReady);
  EXPECT_TRUE(account.snapshot().acquisitionReady);
}
TEST(AccountOrigins, AccountDerivedSecretsAreRevokedAndOldRequestsStayStale) {
  FakeAuth adapter;
  Account account(adapter);
  CredentialCapture c;
  ASSERT_TRUE(capture(c));
  ASSERT_EQ(account.signIn(c, account.snapshot().ticket), AuthError::None);
  ASSERT_EQ(account.useAccountLogin(LoginKind::Sync, account.snapshot().ticket), AuthError::None);
  ASSERT_EQ(account.useAccountLogin(LoginKind::Acquisition, account.snapshot().ticket), AuthError::None);
  const auto before = account.snapshot().ticket;
  EXPECT_EQ(account.snapshot().syncOrigin, LoginOrigin::Account);
  EXPECT_EQ(account.signOut(before), AuthError::None);
  EXPECT_FALSE(account.snapshot().syncReady || account.snapshot().acquisitionReady);
  EXPECT_EQ(account.useAccountLogin(LoginKind::Sync, before), AuthError::Stale);
}

TEST(AccountDelivery, StaleCompletionSettlesCanceledWithoutPublishingReady) {
  FakeAuth adapter;
  Account account(adapter);
  CredentialCapture input;
  ASSERT_TRUE(capture(input));
  ASSERT_EQ(account.signIn(input, account.snapshot().ticket), AuthError::None);
  const auto old = account.snapshot().ticket;
  ASSERT_EQ(account.signOut(old), AuthError::None);
  ASSERT_TRUE(capture(input));
  ASSERT_EQ(account.signIn(input, account.snapshot().ticket), AuthError::None);
  struct Publication {
    unsigned ready = 0, canceled = 0;
  } publication;
  Delivery ticket(old, 0, DeliveryPolicy::Owner,
                  {[](void* context, DeliveryResult result) {
                     auto& value = *static_cast<Publication*>(context);
                     if (result == DeliveryResult::Ready) ++value.ready;
                     if (result == DeliveryResult::Canceled) ++value.canceled;
                   },
                   &publication});
  ticket.poll(1, DeliveryResult::Ready, account.snapshot().ticket);
  ticket.poll(2, DeliveryResult::Ready, account.snapshot().ticket);
  EXPECT_FALSE(ticket.polling());
  EXPECT_EQ(publication.ready, 0u);
  EXPECT_EQ(publication.canceled, 1u);
}

// Android 0.12 sign-out policy: each connector is absent, account-supplied or
// independently entered. UI approval must not change these retention semantics.
class AccountSignOutOrigins : public ::testing::TestWithParam<unsigned> {};
TEST_P(AccountSignOutOrigins, RemovesOnlyAccountSuppliedLoginsAndRetiresTheEpoch) {
  FakeAuth adapter;
  Account account(adapter);
  CredentialCapture input;
  ASSERT_TRUE(capture(input));
  ASSERT_EQ(account.signIn(input, account.snapshot().ticket), AuthError::None);
  const unsigned sync = GetParam() / 3, acquisition = GetParam() % 3;
  for (const auto kind : {LoginKind::Sync, LoginKind::Acquisition}) {
    const auto origin = kind == LoginKind::Sync ? sync : acquisition;
    if (origin == 1) {
      ASSERT_EQ(account.useAccountLogin(kind, account.snapshot().ticket), AuthError::None);
    } else if (origin == 2) {
      ASSERT_TRUE(capture(input, kind));
      ASSERT_EQ(account.signIn(input, account.snapshot().ticket), AuthError::None);
    }
  }
  const auto before = account.snapshot();
  ASSERT_EQ(account.signOut(before.ticket), AuthError::None);
  const auto after = account.snapshot();
  EXPECT_FALSE(after.signedIn);
  EXPECT_EQ(after.accountOwner, 0u);
  EXPECT_EQ(after.syncReady, sync == 2);
  EXPECT_EQ(after.acquisitionReady, acquisition == 2);
  EXPECT_EQ(after.syncOwner, sync == 2 ? before.syncOwner : 0u);
  EXPECT_EQ(after.acquisitionOwner, acquisition == 2 ? before.acquisitionOwner : 0u);
  EXPECT_EQ(after.syncOrigin, sync == 2 ? LoginOrigin::User : LoginOrigin::None);
  EXPECT_EQ(after.acquisitionOrigin, acquisition == 2 ? LoginOrigin::User : LoginOrigin::None);
  EXPECT_GT(after.ticket.epoch, before.ticket.epoch);
  EXPECT_EQ(adapter.clears, 1u);
  EXPECT_EQ(account.signOut(before.ticket), AuthError::Stale);
  EXPECT_EQ(account.removeLogin(LoginKind::Sync, before.ticket), AuthError::Stale);
  EXPECT_EQ(account.useAccountLogin(LoginKind::Acquisition, before.ticket), AuthError::Stale);
  EXPECT_EQ(adapter.clears, 1u);
}
INSTANTIATE_TEST_SUITE_P(AllNinePairs, AccountSignOutOrigins, ::testing::Range(0u, 9u));
