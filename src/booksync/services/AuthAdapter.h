// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace booksync {
enum class LoginKind : uint8_t { Account, Sync, Acquisition };
enum class LoginOrigin : uint8_t { None, User, Account };
enum class AuthError : uint8_t {
  None,
  Unsupported,
  Unavailable,
  Invalid,
  InvalidResponse,
  Unauthorized,
  Forbidden,
  AttestationRequired,
  TooOld,
  Retryable,
  Stale,
  Exhausted
};
struct AuthCapabilities {
  bool account = false;
  bool sync = false;
  bool acquisition = false;
  bool additionalAuthorization = false;
};
struct AuthResult {
  AuthError error = AuthError::Unsupported;
  // Nonzero process-local identity assigned by the adapter, not a remote ID.
  uint32_t owner = 0;
  // Optional account-derived connectors; never replace explicit user logins.
  uint32_t syncOwner = 0;
  uint32_t acquisitionOwner = 0;
};
// Private, borrowed restore-only authorization. The caller erases both buffers
// after the synchronous call; adapters may not retain these views.
struct LoginAuthorization {
  std::string_view identity{}, secret{};
  bool empty() const { return identity.empty() && secret.empty(); }
};
void eraseSecret(void* data, size_t bytes);
// Worker-owned, bounded storage. No heap, persistence, logging or copy/move.
// Keep this object in the owning job, not on a small task stack. Inputs are
// borrowed only by capture(); the caller's mutable secret is erased on all exits.
class CredentialCapture final {
 public:
  static constexpr size_t SecretCapacity = 1024;
  static constexpr size_t OriginCapacity = 512;
  static constexpr size_t UserCapacity = 100;
  CredentialCapture() = default;
  ~CredentialCapture() { clear(); }
  CredentialCapture(const CredentialCapture&) = delete;
  CredentialCapture& operator=(const CredentialCapture&) = delete;
  CredentialCapture(CredentialCapture&&) = delete;
  CredentialCapture& operator=(CredentialCapture&&) = delete;
  bool capture(LoginKind kind, std::string_view origin, std::string_view user, char* secret, size_t size,
               bool derived = false);
  void clear();
  bool ready() const { return secretSize != 0; }
  LoginKind kind() const { return loginKind; }
  bool derived() const { return isDerived; }
  std::string_view origin() const { return {originBytes, originSize}; }
  std::string_view user() const { return {userBytes, userSize}; }
  std::string_view secret() const { return {secretBytes, secretSize}; }

 private:
  char secretBytes[SecretCapacity]{};
  char originBytes[OriginCapacity]{};
  char userBytes[UserCapacity]{};
  size_t secretSize = 0, originSize = 0, userSize = 0;
  LoginKind loginKind = LoginKind::Account;
  bool isDerived = false;
};
static_assert(sizeof(CredentialCapture) <= 2048);
// Worker-only account boundary; UI receives values through Library. Auth never
// writes library data and never mints the serial sync writer capability.
class AuthAdapter {
 public:
  virtual ~AuthAdapter() = default;
  virtual AuthCapabilities authCapabilities() const { return {}; }
  // Borrowed input must not be retained. Implementations commit only fully
  // validated sessions; failed attempts leave the previous session untouched.
  virtual AuthResult authenticate(const CredentialCapture&) { return {}; }
  // Offline import/boot restore only. Must not issue authentication requests.
  virtual AuthResult restoreLogin(const CredentialCapture&) { return {}; }
  virtual AuthResult restoreLogin(const CredentialCapture& input, LoginAuthorization authorization) {
    return authorization.empty() ? restoreLogin(input) : AuthResult{};
  }
  virtual AuthResult useAccountLogin(LoginKind) { return {}; }
  virtual AuthError removeLogin(LoginKind) { return AuthError::Unsupported; }
  // Shared adapters must revoke this connector only. The compatibility default
  // keeps adapters with one private session unchanged.
  virtual AuthError removeLogin(LoginKind kind, uint32_t) { return removeLogin(kind); }
  // Local revocation is mandatory, synchronous and cannot depend on connectivity.
  // Erase account tokens/candidates and account-derived connector secrets.
  // Explicit user logins remain independent; removeLogin erases those separately.
  virtual void clearAuth() {}
};
}  // namespace booksync
