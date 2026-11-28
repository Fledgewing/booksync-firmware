// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>

namespace booksync::import {
enum class IdentityStatus : uint8_t { Absent, RetainedInPlace, Incomplete, Unsupported, Corrupt, ReadError };
// Private migration evidence, never a UI value or diagnostic log. The digest is
// over the six opaque blobs in canonical order, not a new key or device name.
// This is a layout/retention audit, not cryptographic verification of key pairs.
struct IdentityAudit {
  static constexpr uint8_t version = 1;
  IdentityStatus status = IdentityStatus::ReadError;
  uint8_t identityBlobs = 0;
  bool joined = false;
  std::array<uint8_t, 32> fingerprint{};
};
#if !defined(SIMULATOR)
// Worker-only while Tailnet is stopped. Existing NVS is the destination: there
// is no copy, repair, activation, write, erase or key generation in this import.
IdentityAudit auditIdentity();
#endif
}  // namespace booksync::import
