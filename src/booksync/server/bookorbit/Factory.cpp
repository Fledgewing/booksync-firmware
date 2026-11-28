#include "Auth.h"
#include "booksync/services/ServerAdapter.h"

namespace booksync {
namespace {
class RetainedAuthTransport final : public server::AuthTransport {
 public:
  uint32_t nextOwner = 1;
  AuthCapabilities capabilities() const override { return {false, true, true, true}; }
  // Only local, validated captures enter this seam. Durable source activation
  // and retirement remain with the worker; do not create a web-account session
  // or make an authentication request while restoring an independent login.
  server::AuthReply restoreLogin(LoginKind kind, std::string_view origin, std::string_view user,
                                 std::string_view secret, bool derived) override {
    if (kind == LoginKind::Account || origin.empty() || user.empty() || secret.empty() || !nextOwner ||
        (kind == LoginKind::Sync && (!derived || secret.size() != 32)))
      return {};
    return {200, false, nextOwner++};
  }
  server::AuthReply restoreAuthorizedLogin(LoginKind kind, std::string_view origin, std::string_view user,
                                           std::string_view secret, bool derived, std::string_view identity,
                                           std::string_view authorization) override {
    if (identity.empty() || authorization.empty()) return {};
    return restoreLogin(kind, origin, user, secret, derived);
  }
  AuthError removeLogin(LoginKind kind) override {
    return kind == LoginKind::Account ? AuthError::Invalid : AuthError::None;
  }
  server::AuthReply authenticate(LoginKind, std::string_view, std::string_view, std::string_view, bool) override {
    return {};
  }
  void finishAuth(bool) override {}
  void clearAuth() override {}
};
class OfflineAdapter final : public ServerAdapter {
  RetainedAuthTransport transport;
  server::Auth auth{transport};

 public:
  const char* serviceLabel() const override { return "BookOrbit"; }
  AuthCapabilities authCapabilities() const override { return auth.authCapabilities(); }
  AuthResult authenticate(const CredentialCapture& input) override { return auth.authenticate(input); }
  AuthResult restoreLogin(const CredentialCapture& input) override { return auth.restoreLogin(input); }
  AuthResult restoreLogin(const CredentialCapture& input, LoginAuthorization authorization) override {
    return auth.restoreLogin(input, authorization);
  }
  void clearAuth() override { auth.clearAuth(); }
  AuthResult useAccountLogin(LoginKind kind) override { return auth.useAccountLogin(kind); }
  AuthError removeLogin(LoginKind kind) override { return auth.removeLogin(kind); }
  Capabilities capabilities() const override { return {}; }
  Snapshot cachedSnapshot() const override { return {}; }
  CommandStatus deliver(const SyncWriterToken&, Intent) override { return CommandStatus::Unavailable; }
};
}  // namespace

ServerAdapter& defaultServerAdapter() {
  static OfflineAdapter adapter;
  return adapter;
}
}  // namespace booksync
