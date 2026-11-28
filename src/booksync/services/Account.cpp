// SPDX-License-Identifier: GPL-3.0-or-later
#include "Account.h"

#include <cstring>
#include <limits>

namespace booksync {
void eraseSecret(void* data, size_t bytes) {
  auto* p = static_cast<volatile unsigned char*>(data);
  if (p)
    while (bytes--) *p++ = 0;
}
void CredentialCapture::clear() {
  eraseSecret(secretBytes, sizeof(secretBytes));
  eraseSecret(originBytes, sizeof(originBytes));
  eraseSecret(userBytes, sizeof(userBytes));
  secretSize = originSize = userSize = 0;
  loginKind = LoginKind::Account;
  isDerived = false;
}
bool CredentialCapture::capture(LoginKind kind, std::string_view origin, std::string_view user, char* secret,
                                size_t size, bool derived) {
  clear();
  const bool valid = secret && size && size <= SecretCapacity && !origin.empty() && origin.size() <= OriginCapacity &&
                     !user.empty() && user.size() <= UserCapacity && origin.find('\0') == std::string_view::npos &&
                     user.find('\0') == std::string_view::npos &&
                     (kind == LoginKind::Account || kind == LoginKind::Sync || kind == LoginKind::Acquisition) &&
                     (!derived || kind == LoginKind::Sync);
  if (valid) {
    std::memcpy(secretBytes, secret, size);
    std::memcpy(originBytes, origin.data(), origin.size());
    std::memcpy(userBytes, user.data(), user.size());
    secretSize = size;
    originSize = origin.size();
    userSize = user.size();
    loginKind = kind;
    isDerived = derived;
  }
  eraseSecret(secret, size);
  return valid;
}
AccountSnapshot Account::snapshot() const {
  auto copy = state;
  copy.capabilities = adapter.authCapabilities();
  return copy;
}
bool Account::current(DeliveryTicket expected) const {
  return expected.owner == state.ticket.owner && expected.epoch == state.ticket.epoch;
}
AuthError Account::signIn(CredentialCapture& input, DeliveryTicket expected) {
  return acceptLogin(input, expected, false);
}
AuthError Account::restoreLogin(CredentialCapture& input, DeliveryTicket expected, LoginAuthorization authorization) {
  // Import only fills absent independent connector slots. It never revives a
  // web account or replaces a current login in an already-running worker.
  if (input.kind() == LoginKind::Account || (input.kind() == LoginKind::Sync && state.syncReady) ||
      (input.kind() == LoginKind::Acquisition && state.acquisitionReady)) {
    input.clear();
    return AuthError::Stale;
  }
  if (!authorization.empty() &&
      (authorization.identity.empty() || authorization.secret.empty() || authorization.identity.size() > 256 ||
       authorization.secret.size() > CredentialCapture::SecretCapacity ||
       authorization.identity.find('\0') != std::string_view::npos ||
       authorization.secret.find('\0') != std::string_view::npos)) {
    input.clear();
    return AuthError::Invalid;
  }
  return acceptLogin(input, expected, true, authorization);
}
AuthError Account::acceptLogin(CredentialCapture& input, DeliveryTicket expected, bool restore,
                               LoginAuthorization authorization) {
  // Erasure is unconditional, including rejected/stale requests and unsupported adapters.
  struct Cleanup {
    CredentialCapture& input;
    ~Cleanup() { input.clear(); }
  } cleanup{input};
  if (!current(expected)) return AuthError::Stale;
  if (state.ticket.epoch == std::numeric_limits<uint64_t>::max()) return AuthError::Exhausted;
  if (!input.ready()) return AuthError::Invalid;
  const auto capabilities = adapter.authCapabilities();
  if ((input.kind() == LoginKind::Account && !capabilities.account) ||
      (input.kind() == LoginKind::Sync && !capabilities.sync) ||
      (input.kind() == LoginKind::Acquisition && !capabilities.acquisition))
    return AuthError::Unsupported;
  const auto result = restore ? adapter.restoreLogin(input, authorization) : adapter.authenticate(input);
  if (result.error != AuthError::None) return result.error;
  if (!result.owner) return AuthError::InvalidResponse;
  // Even reauthentication of the same owner retires all old result tickets.
  ++state.ticket.epoch;
  if (input.kind() == LoginKind::Account) {
    state.accountOwner = result.owner;
    state.signedIn = true;
    if (state.syncOrigin != LoginOrigin::User)
      setConnector(LoginKind::Sync, result.syncOwner, result.syncOwner ? LoginOrigin::Account : LoginOrigin::None);
    if (state.acquisitionOrigin != LoginOrigin::User)
      setConnector(LoginKind::Acquisition, result.acquisitionOwner,
                   result.acquisitionOwner ? LoginOrigin::Account : LoginOrigin::None);
  } else {
    setConnector(input.kind(), result.owner, LoginOrigin::User);
  }
  publishOwner();
  return AuthError::None;
}
AuthError Account::signOut(DeliveryTicket expected) {
  if (!current(expected)) return AuthError::Stale;
  // Clear locally even at epoch exhaustion. Admission then remains closed forever.
  adapter.clearAuth();
  state.signedIn = false;
  state.accountOwner = 0;
  if (state.syncOrigin == LoginOrigin::Account) setConnector(LoginKind::Sync, 0, LoginOrigin::None);
  if (state.acquisitionOrigin == LoginOrigin::Account) setConnector(LoginKind::Acquisition, 0, LoginOrigin::None);
  publishOwner();
  if (state.ticket.epoch == std::numeric_limits<uint64_t>::max()) return AuthError::Exhausted;
  ++state.ticket.epoch;
  return AuthError::None;
}
void Account::setConnector(LoginKind kind, uint32_t owner, LoginOrigin origin) {
  if (kind == LoginKind::Sync) {
    state.syncOwner = owner;
    state.syncReady = owner != 0;
    state.syncOrigin = origin;
  } else {
    state.acquisitionOwner = owner;
    state.acquisitionReady = owner != 0;
    state.acquisitionOrigin = origin;
  }
}
void Account::publishOwner() {
  state.ticket.owner = state.acquisitionOwner ? state.acquisitionOwner
                       : state.syncOwner      ? state.syncOwner
                                              : state.accountOwner;
}
AuthError Account::removeLogin(LoginKind kind, DeliveryTicket expected) {
  if (!current(expected)) return AuthError::Stale;
  if (state.ticket.epoch == std::numeric_limits<uint64_t>::max()) return AuthError::Exhausted;
  if (kind != LoginKind::Sync && kind != LoginKind::Acquisition) return AuthError::Invalid;
  const unsigned slot = kind == LoginKind::Sync ? 0 : 1;
  if (retirer_[slot] && !retirer_[slot](kind, retirement_[slot])) return AuthError::Unavailable;
  const auto error = adapter.removeLogin(kind);
  if (error != AuthError::None) return error;
  setConnector(kind, 0, LoginOrigin::None);
  ++state.ticket.epoch;
  publishOwner();
  return AuthError::None;
}
AuthError Account::useAccountLogin(LoginKind kind, DeliveryTicket expected) {
  if (!current(expected)) return AuthError::Stale;
  if (state.ticket.epoch == std::numeric_limits<uint64_t>::max()) return AuthError::Exhausted;
  if (kind != LoginKind::Sync && kind != LoginKind::Acquisition) return AuthError::Invalid;
  if (!state.signedIn) return AuthError::Unauthorized;
  const auto result = adapter.useAccountLogin(kind);
  if (result.error != AuthError::None) return result.error;
  if (!result.owner) return AuthError::InvalidResponse;
  setConnector(kind, result.owner, LoginOrigin::Account);
  ++state.ticket.epoch;
  publishOwner();
  return AuthError::None;
}
}  // namespace booksync
