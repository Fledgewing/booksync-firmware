// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from BookSync; Ed25519 field/group arithmetic derives from public-domain TweetNaCl.
#pragma once
// BookSync: Ed25519 signature verification (RFC 8032, pure Ed25519), for signed updates
// (docs/design/release-signing.md). Verify only, no signing and no secrets, so constant time is
// not needed; small over fast: the X3's flash is nearly full. The field and group arithmetic is
// TweetNaCl's (public domain); SHA-512 is mbedTLS's on the device and the built-in one elsewhere.
#include <cstddef>
#include <cstdint>

namespace booksync::ed25519 {

// Longest message verify() takes (BookSync signs short records, never whole files).
constexpr size_t kMaxMessage = 256;

// True when `signature` (R || S, 64 bytes) is `publicKey`'s signature over `message`. False for a
// bad signature, a non-canonical S, a public key that is not a curve point, a message longer
// than kMaxMessage, or no memory for the work area.
bool verify(const uint8_t signature[64], const uint8_t* message, size_t length, const uint8_t publicKey[32]);

// SHA-512 of `data` (exposed for the host test's known-answer check).
void sha512(const uint8_t* data, size_t length, uint8_t out[64]);

}  // namespace booksync::ed25519
