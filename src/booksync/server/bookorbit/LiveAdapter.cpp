// SPDX-License-Identifier: GPL-3.0-or-later
#include "LiveAdapter.h"

#include <Logging.h>
#include <Memory.h>

#include <cstring>

namespace booksync::server {
namespace {
template <size_t N>
bool copy(std::array<char, N>& out, std::string_view value) {
  if (value.size() >= N || value.find_first_of(std::string_view("\r\n\0", 3)) != value.npos) return false;
  if (!value.empty()) memcpy(out.data(), value.data(), value.size());
  out[value.size()] = 0;
  return true;
}
}  // namespace
struct LiveAdapter::Cursor final : CatalogTransport {
  HttpPort& http;
  HttpLogin* login = nullptr;
  int status = 0;
  Catalog catalog{*this};
  explicit Cursor(HttpPort& port) : http(port) { static_assert(sizeof(Cursor) <= 5 * 1024); }
  int open(std::string_view path) override {
    status = login ? http.open(*login, "GET", path, {}, Catalog::MaxPageBytes) : 0;
    return status;
  }
  int read(char* out, size_t count) override { return http.read(out, count); }
  int finish() override { return http.finish(); }
  void close() override { http.close(); }
};
LiveAdapter::LiveAdapter(HttpPort& http) : http_(http) {}
LiveAdapter::~LiveAdapter() { endSyncSession(); }
AuthReply LiveAdapter::stage(LoginKind kind, std::string_view origin, std::string_view user, std::string_view secret,
                             bool derived, std::string_view identity, std::string_view authorization) {
  candidate_.reset();
  if (kind == LoginKind::Account || origin.empty() || user.empty() || secret.empty() || !nextOwner_ ||
      (kind == LoginKind::Sync && (!derived || secret.size() != 32)) || identity.empty() != authorization.empty())
    return {};
  bool room = false;
  for (const auto& login : logins_) room |= !login;
  if (!room) {
    LOG_ERR("BSA", "Private connector capacity reached");
    return {};
  }
  // <=3 KiB per present connector, at most ten. Credentials must survive boot
  // restore's erased buffers; stack cannot own them and absent slots cost only a
  // pointer. No allocation per request, catalog row or retry.
  candidate_ = makeUniqueNoThrow<HttpLogin>();
  if (!candidate_) {
    LOG_ERR("BSA", "Cannot retain private connector (%u bytes)", unsigned(sizeof(HttpLogin)));
    return {};
  }
  auto& value = *candidate_;
  if (!copy(value.origin, origin) || !copy(value.user, user) || !copy(value.secret, secret) ||
      !copy(value.authorizationUser, identity) || !copy(value.authorizationSecret, authorization)) {
    candidate_.reset();
    return {};
  }
  value.kind = kind;
  value.owner = nextOwner_++;
  return {200, false, value.owner};
}
void LiveAdapter::finishAuth(bool accept) {
  if (candidate_ && accept) {
    for (auto& login : logins_) {
      if (!login) {
        login = std::move(candidate_);
        break;
      }
    }
  }
  candidate_.reset();
}
AuthError LiveAdapter::removeLogin(LoginKind kind, uint32_t owner) {
  if (kind == LoginKind::Account) return AuthError::Invalid;
  for (auto& login : logins_) {
    if (login && login->owner == owner && login->kind == kind) {
      if (selected_ == login.get()) {
        endSyncSession();
      }
      login.reset();
      return AuthError::None;
    }
  }
  return owner ? AuthError::Stale : AuthError::None;
}
bool LiveAdapter::selectSyncSession(uint32_t owner) {
  if (cursor_) cursor_->catalog.cancel();
  selected_ = nullptr;
  if (cursor_) cursor_->login = nullptr;
  for (auto& login : logins_) {
    if (login && login->owner == owner && login->kind == LoginKind::Sync) {
      if (!cursor_) {
        // <=5 KiB parser/cursor workspace, too large for the 6 KiB worker stack.
        // Allocate once per worker session, reuse for all pages, release before
        // returning network ownership so reading retains no parser workspace.
        cursor_ = makeUniqueNoThrow<Cursor>(http_);
        if (!cursor_) {
          LOG_ERR("BSC", "Cannot allocate catalog cursor (%u bytes)", unsigned(sizeof(Cursor)));
          return false;
        }
      }
      selected_ = login.get();
      cursor_->login = selected_;
      cursor_->status = 0;
      return true;
    }
  }
  return false;
}
void LiveAdapter::endSyncSession() {
  if (cursor_) cursor_->catalog.cancel();
  cursor_.reset();
  http_.release();
  selected_ = nullptr;
}
bool LiveAdapter::startCatalog(std::string_view device) {
  return selected_ && cursor_ && cursor_->catalog.start({device, "original", CatalogFeed::Inventory});
}
CatalogSource* LiveAdapter::catalogSource() { return selected_ && cursor_ ? &cursor_->catalog : nullptr; }
AuthError LiveAdapter::sessionError() const {
  return selected_ && cursor_ ? normalizeAuthReply(cursor_->status, false) : AuthError::Unavailable;
}
}  // namespace booksync::server
