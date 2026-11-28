#include "Auth.h"
#include "booksync/services/ServerAdapter.h"

namespace booksync {
namespace {
class OfflineAuthTransport final : public server::AuthTransport {
 public:
  AuthCapabilities capabilities() const override { return {}; }
  server::AuthReply authenticate(LoginKind, std::string_view, std::string_view, std::string_view, bool) override {
    return {};
  }
  void finishAuth(bool) override {}
  void clearAuth() override {}
};
class OfflineAdapter final : public ServerAdapter {
  OfflineAuthTransport transport;
  server::Auth auth{transport};

 public:
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
