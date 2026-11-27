// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/services/AuthAdapter.h"

namespace booksync::server {
AuthError normalizeAuthReply(int status, bool attestationRequired);
struct AuthReply {
  int status = 0;
  bool attestationRequired = false;
  uint32_t owner = 0;
  uint32_t syncOwner = 0;
  uint32_t acquisitionOwner = 0;
};
// Private plugin transport seam. No route, credential, response body or session
// handle crosses the server interface. Account uses native login only; sync uses
// the legacy derived key and acquisition its independent login. Never retry with
// another login type or probe/forge an attested route.
class AuthTransport {
 public:
  virtual ~AuthTransport() = default;
  virtual AuthCapabilities capabilities() const = 0;
  virtual AuthReply authenticate(LoginKind kind, std::string_view origin, std::string_view user,
                                 std::string_view secret, bool derived) = 0;
  // authenticate stages private candidates. Only accept=true commits; false
  // erases staged secrets and preserves the previous account. No plaintext store.
  virtual void finishAuth(bool accept) = 0;
  // Account sign-out erases its session and derived slots; user slots survive.
  virtual void clearAuth() = 0;
  virtual AuthReply useAccountLogin(LoginKind) { return {}; }
  virtual AuthError removeLogin(LoginKind) { return AuthError::Unsupported; }
};
class Auth final : public AuthAdapter {
 public:
  explicit Auth(AuthTransport& transport) : transport(transport) {}
  AuthCapabilities authCapabilities() const override { return transport.capabilities(); }
  AuthResult authenticate(const CredentialCapture& input) override;
  void clearAuth() override { transport.clearAuth(); }
  AuthResult useAccountLogin(LoginKind kind) override;
  AuthError removeLogin(LoginKind kind) override;

 private:
  AuthResult complete(AuthReply reply);
  AuthTransport& transport;
};
}  // namespace booksync::server
