// SPDX-License-Identifier: GPL-3.0-or-later
#include "Auth.h"
#if defined(SIMULATOR) && defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#elif defined(SIMULATOR)
#include <openssl/md5.h>
#else
#include <mbedtls/md5.h>
#endif

namespace booksync::server {
AuthError normalizeAuthReply(int status, bool attestationRequired) {
  if (attestationRequired) return AuthError::AttestationRequired;
  if (status >= 200 && status < 300) return AuthError::None;
  switch (status) {
    case 401:
      return AuthError::Unauthorized;
    case 403:
      return AuthError::Forbidden;
    case 404:
    case 426:
      return AuthError::TooOld;
    case 429:
      return AuthError::Retryable;
    default:
      return AuthError::Unavailable;
  }
}
namespace {
bool syncKey(std::string_view input, bool derived, char (&key)[32]) {
  if (derived) {
    if (input.size() != sizeof(key)) return false;
    for (size_t i = 0; i < input.size(); ++i) {
      const char c = input[i];
      if (c >= '0' && c <= '9')
        key[i] = c;
      else if (c >= 'a' && c <= 'f')
        key[i] = c;
      else if (c >= 'A' && c <= 'F')
        key[i] = c + ('a' - 'A');
      else
        return false;
    }
    return true;
  }
  // Legacy sync password derivation is protocol compatibility, not web login
  // authentication or password storage. Its key is itself a secret.
  unsigned char digest[16]{};
#if defined(SIMULATOR) && defined(__APPLE__)
  // Required legacy wire derivation, not a new password hash.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  CC_MD5(input.data(), static_cast<CC_LONG>(input.size()), digest);
#pragma GCC diagnostic pop
#elif defined(SIMULATOR)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  MD5(reinterpret_cast<const unsigned char*>(input.data()), input.size(), digest);
#pragma GCC diagnostic pop
#else
  if (mbedtls_md5(reinterpret_cast<const unsigned char*>(input.data()), input.size(), digest) != 0) {
    eraseSecret(digest, sizeof(digest));
    return false;
  }
#endif
  static constexpr char hex[] = "0123456789abcdef";
  for (size_t i = 0; i < sizeof(digest); ++i) {
    key[i * 2] = hex[digest[i] >> 4];
    key[i * 2 + 1] = hex[digest[i] & 15];
  }
  eraseSecret(digest, sizeof(digest));
  return true;
}
}  // namespace
AuthResult Auth::authenticate(const CredentialCapture& input) { return accept(input, false); }
AuthResult Auth::restoreLogin(const CredentialCapture& input) { return restoreLogin(input, {}); }
AuthResult Auth::restoreLogin(const CredentialCapture& input, LoginAuthorization authorization) {
  if (input.kind() == LoginKind::Account) return {AuthError::Unsupported};
  if (!authorization.empty() && !authCapabilities().additionalAuthorization) return {AuthError::Unsupported};
  return accept(input, true, authorization);
}
AuthResult Auth::accept(const CredentialCapture& input, bool restore, LoginAuthorization authorization) {
  if (!input.ready()) return {AuthError::Invalid};
  const auto capabilities = authCapabilities();
  if ((input.kind() == LoginKind::Account && !capabilities.account) ||
      (input.kind() == LoginKind::Sync && !capabilities.sync) ||
      (input.kind() == LoginKind::Acquisition && !capabilities.acquisition))
    return {};
  char key[32]{};
  const bool sync = input.kind() == LoginKind::Sync;
  if (sync && !syncKey(input.secret(), input.derived(), key)) {
    eraseSecret(key, sizeof(key));
    return {AuthError::Invalid};
  }
  const auto secret = sync ? std::string_view(key, sizeof(key)) : input.secret();
  const auto reply = !restore ? transport.authenticate(input.kind(), input.origin(), input.user(), secret, sync)
                     : authorization.empty()
                         ? transport.restoreLogin(input.kind(), input.origin(), input.user(), secret, sync)
                         : transport.restoreAuthorizedLogin(input.kind(), input.origin(), input.user(), secret, sync,
                                                            authorization.identity, authorization.secret);
  eraseSecret(key, sizeof(key));
  return complete(reply);
}
AuthResult Auth::complete(AuthReply reply) {
  auto error = normalizeAuthReply(reply.status, reply.attestationRequired);
  if (error == AuthError::None && !reply.owner) error = AuthError::InvalidResponse;
  transport.finishAuth(error == AuthError::None);
  if (error != AuthError::None) return {error};
  return {AuthError::None, reply.owner, reply.syncOwner, reply.acquisitionOwner};
}
AuthResult Auth::useAccountLogin(LoginKind kind) {
  const auto capabilities = authCapabilities();
  if ((kind == LoginKind::Sync && !capabilities.sync) || (kind == LoginKind::Acquisition && !capabilities.acquisition))
    return {};
  if (kind != LoginKind::Sync && kind != LoginKind::Acquisition) return {AuthError::Invalid};
  return complete(transport.useAccountLogin(kind));
}
AuthError Auth::removeLogin(LoginKind kind) {
  if (kind != LoginKind::Sync && kind != LoginKind::Acquisition) return AuthError::Invalid;
  return transport.removeLogin(kind);
}
}  // namespace booksync::server
