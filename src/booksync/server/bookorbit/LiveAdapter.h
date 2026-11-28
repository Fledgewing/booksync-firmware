// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <memory>

#include "Auth.h"
#include "Catalog.h"
#include "booksync/services/ServerAdapter.h"

namespace booksync::server {
// Private, bounded copies: restore callers erase their borrowed inputs. Secret
// slots exist only for retained live logins and are wiped on local revocation.
struct HttpLogin final {
  std::array<char, 513> origin{};
  std::array<char, 101> user{};
  std::array<char, 1025> secret{};
  std::array<char, 257> authorizationUser{};
  std::array<char, 1025> authorizationSecret{};
  LoginKind kind = LoginKind::Sync;
  uint32_t owner = 0;
  ~HttpLogin() { eraseSecret(this, sizeof(*this)); }
};
static_assert(sizeof(HttpLogin) <= 3 * 1024);
// Worker-only HTTP body cursor. open returns only after complete framing has
// been verified. Production streams to a bounded disposable SD spool. Nothing
// reads a partial spool or buffers a catalog response in RAM.
class HttpPort {
 public:
  virtual ~HttpPort() = default;
  virtual int open(const HttpLogin&, const char* method, std::string_view path, std::string_view payload,
                   size_t maximum) = 0;
  virtual int read(char*, size_t) = 0;
  virtual int finish() = 0;
  virtual void close() = 0;
  virtual void release() { close(); }
};
HttpPort& deviceHttpPort();

class LiveAdapter final : public ServerAdapter {
 public:
  explicit LiveAdapter(HttpPort& http);
  ~LiveAdapter() override;
  const char* serviceLabel() const override { return "BookOrbit"; }
  AuthCapabilities authCapabilities() const override { return {false, true, true, true}; }
  AuthResult authenticate(const CredentialCapture& input) override { return auth_.authenticate(input); }
  AuthResult restoreLogin(const CredentialCapture& input) override { return auth_.restoreLogin(input); }
  AuthResult restoreLogin(const CredentialCapture& input, LoginAuthorization extra) override {
    return auth_.restoreLogin(input, extra);
  }
  AuthError removeLogin(LoginKind) override { return AuthError::Invalid; }
  AuthError removeLogin(LoginKind, uint32_t owner) override;
  // Account sign-out preserves independent user connectors. This adapter does
  // not advertise or create a web account session.
  void clearAuth() override {}
  Capabilities capabilities() const override { return {true, true, false}; }
  Snapshot cachedSnapshot() const override { return {}; }
  bool selectSyncSession(uint32_t owner) override;
  void endSyncSession() override;
  bool startCatalog(std::string_view device) override;
  CatalogSource* catalogSource() override;
  AuthError sessionError() const override;
  CommandStatus deliver(const SyncWriterToken&, Intent) override { return CommandStatus::Unavailable; }

 private:
  struct Cursor;
  class LoginTransport final : public AuthTransport {
   public:
    explicit LoginTransport(LiveAdapter& adapter) : adapter_(adapter) {}
    AuthCapabilities capabilities() const override { return adapter_.authCapabilities(); }
    AuthReply authenticate(LoginKind, std::string_view, std::string_view, std::string_view, bool) override {
      return {};
    }
    AuthReply restoreLogin(LoginKind kind, std::string_view origin, std::string_view user, std::string_view secret,
                           bool derived) override {
      return adapter_.stage(kind, origin, user, secret, derived, {}, {});
    }
    AuthReply restoreAuthorizedLogin(LoginKind kind, std::string_view origin, std::string_view user,
                                     std::string_view secret, bool derived, std::string_view identity,
                                     std::string_view authorization) override {
      return adapter_.stage(kind, origin, user, secret, derived, identity, authorization);
    }
    void finishAuth(bool accept) override { adapter_.finishAuth(accept); }
    void clearAuth() override {}

   private:
    LiveAdapter& adapter_;
  };
  HttpPort& http_;
  LoginTransport authTransport_{*this};
  Auth auth_{authTransport_};
  std::array<std::unique_ptr<HttpLogin>, 10> logins_{};
  std::unique_ptr<HttpLogin> candidate_;
  std::unique_ptr<Cursor> cursor_;
  HttpLogin* selected_ = nullptr;
  uint32_t nextOwner_ = 1;
  AuthReply stage(LoginKind, std::string_view, std::string_view, std::string_view, bool, std::string_view,
                  std::string_view);
  void finishAuth(bool);
};
}  // namespace booksync::server
