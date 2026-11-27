// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from BookSync; Ed25519 field/group arithmetic derives from public-domain TweetNaCl.
#include "UpdateVerify.h"

#include <cstring>

#include "Ed25519Verify.h"
#include "Feed.h"

namespace booksync::update_signature {

namespace {
constexpr char kCertificateDomain[] = "orbify-release-key-v1\n";
constexpr char kArtifactDomain[] = "orbify-artifact-v1\n";

int hexValue(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Appends `text` to buf at *len; false when it would not fit.
bool append(uint8_t* buf, size_t* len, const char* text, size_t n) {
  if (*len + n > ed25519::kMaxMessage) return false;
  memcpy(buf + *len, text, n);
  *len += n;
  return true;
}
}  // namespace

bool decodeHex(const char* hex, uint8_t* out, size_t n) {
  if (hex == nullptr) return false;
  for (size_t i = 0; i < n; ++i) {
    const int hi = hexValue(hex[2 * i]);
    const int lo = hi < 0 ? -1 : hexValue(hex[2 * i + 1]);
    if (lo < 0) return false;
    out[i] = (uint8_t)(hi << 4 | lo);
  }
  return hex[2 * n] == '\0';
}

Result check(const uint8_t* signature, const Trust& trust, const char* tag, const char* asset, const char* sha256Hex) {
  if (trust.root == nullptr) return Result::NoRoot;
  if (signature == nullptr) return Result::Unsigned;
  const uint8_t* cert = signature;
  const uint32_t serial = (uint32_t)cert[0] << 24 | (uint32_t)cert[1] << 16 | (uint32_t)cert[2] << 8 | cert[3];
  if (serial < trust.minSerial) return Result::Revoked;

  uint8_t message[ed25519::kMaxMessage];
  size_t len = 0;
  append(message, &len, kCertificateDomain, sizeof(kCertificateDomain) - 1);
  append(message, &len, reinterpret_cast<const char*>(cert), 36);
  if (!ed25519::verify(cert + 36, message, len, trust.root)) return Result::BadCertificate;

  if (tag == nullptr || asset == nullptr || strchr(tag, '\n') || strchr(tag, '\r') || strchr(asset, '\n') ||
      strchr(asset, '\r') || sha256Hex == nullptr || strlen(sha256Hex) != 64)
    return Result::BadSignature;
  char sha[65];
  for (int i = 0; i < 64; ++i) {
    const char c = sha256Hex[i];
    if (hexValue(c) < 0) return Result::BadSignature;
    sha[i] = (c >= 'A' && c <= 'F') ? (char)(c - 'A' + 'a') : c;
  }
  sha[64] = '\n';
  len = 0;
  const bool fits = append(message, &len, kArtifactDomain, sizeof(kArtifactDomain) - 1) &&
                    append(message, &len, tag, strlen(tag)) && append(message, &len, "\n", 1) &&
                    append(message, &len, asset, strlen(asset)) && append(message, &len, "\n", 1) &&
                    append(message, &len, sha, sizeof(sha));
  if (!fits || !ed25519::verify(signature + kCertificateBytes, message, len, cert + 4)) return Result::BadSignature;
  return Result::Ok;
}

Result checkDownload(const char* rootHex, uint32_t minSerial, const char* tag, const char* url, const char* sha256Hex,
                     const uint8_t* signature, const char* expectedDeviceStem) {
  uint8_t root[32];
  const bool haveRoot = decodeHex(rootHex, root, sizeof(root));
  const char* slash = url != nullptr ? strrchr(url, '/') : nullptr;
  const char* asset = slash != nullptr ? slash + 1 : url;
  if (haveRoot) {
    if (asset == nullptr || expectedDeviceStem == nullptr) return Result::BadSignature;
    const size_t stemLength = strlen(expectedDeviceStem);
    if (strncmp(asset, expectedDeviceStem, stemLength) != 0) return Result::BadSignature;
    const char* suffix = asset + stemLength;
    const size_t length = strlen(suffix);
    if (strcmp(suffix, ".bin") != 0 && (length < 5 || suffix[0] != '-' || strcmp(suffix + length - 4, ".bin") != 0))
      return Result::BadSignature;
  }
  return check(signature, Trust{haveRoot ? root : nullptr, minSerial}, tag, asset, sha256Hex);
}

Result checkStaged(const char* rootHex, uint32_t minSerial, const char* tag, const char* url,
                   const char* expectedSha256, const uint8_t* signature, const char* deviceStem,
                   const uint8_t actualSha256[32], size_t actualSize, size_t expectedSize) {
  uint8_t expected[32];
  if (!actualSha256 || !decodeHex(expectedSha256, expected, sizeof(expected)) ||
      memcmp(expected, actualSha256, sizeof(expected)) != 0)
    return Result::HashMismatch;
  if (!actualSize || (expectedSize && actualSize != expectedSize)) return Result::SizeMismatch;
  return checkDownload(rootHex, minSerial, tag, url, expectedSha256, signature, deviceStem);
}
bool decodeDetached(const char* text, size_t length, const char* asset, uint8_t out[kSignatureBytes]) {
  if (!text || !asset || !out) return false;
  constexpr size_t certHex = 2 * kCertificateBytes;
  constexpr size_t sigHex = 128;
  constexpr char prefix[] = "release-key ";
  constexpr size_t prefixLength = sizeof(prefix) - 1;
  const size_t assetLength = strlen(asset);
  // Both workflow lines end in LF. Also accept EOF immediately after the asset.
  const size_t expected = prefixLength + certHex + 1 + sigHex + 2 + assetLength;
  if ((length != expected && length != expected + 1) || memcmp(text, prefix, prefixLength) ||
      text[prefixLength + certHex] != '\n' || (length == expected + 1 && text[expected] != '\n'))
    return false;
  const char* signature = text + prefixLength + certHex + 1;
  if (signature[sigHex] != ' ' || signature[sigHex + 1] != ' ' || memcmp(signature + sigHex + 2, asset, assetLength))
    return false;
  for (size_t i = 0; i < kSignatureBytes; ++i) {
    const char* hex = i < kCertificateBytes ? text + prefixLength + 2 * i : signature + 2 * (i - kCertificateBytes);
    const int hi = hexValue(hex[0]), lo = hexValue(hex[1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}

Result checkFeed(const uint8_t* signature, const Trust& trust, const char* actualSha256Hex) {
  return check(signature, trust, "xteink-feed", "releases.json", actualSha256Hex);
}

const char* describe(Result result) {
  switch (result) {
    case Result::HashMismatch:
      return "staged digest mismatch";
    case Result::SizeMismatch:
      return "staged length mismatch";
    case Result::Ok:
      return "signed";
    case Result::NoRoot:
      return "unsigned (this build has no root key)";
    case Result::Unsigned:
      return "unsigned";
    case Result::Revoked:
      return "signature invalid: revoked release key";
    case Result::BadCertificate:
      return "signature invalid: release key not certified by the root";
    case Result::BadSignature:
      return "signature invalid";
  }
  return "signature invalid";
}

}  // namespace booksync::update_signature

// Link-only budget root: never invoked at runtime; no URL, network or installer.
// Retain keeps the parser/verifier in application size measurements on GCC and Clang.
extern "C" __attribute__((used, retain)) bool booksync_update_trust_link(booksync::Feed& parser, const char* data,
                                                                         size_t length, const uint8_t* signature,
                                                                         const uint8_t digest[32]) {
  parser.reset();
  parser.feedResponse(200, data, length);
  const auto& r = parser.release();
  namespace sig = booksync::update_signature;
  uint8_t detached[sig::kSignatureBytes];
  uint8_t root[32];
  if (sig::decodeDetached(data, length, "releases.json", detached) && sig::decodeHex(sig::kRootHex, root, sizeof(root)))
    return sig::accepted(sig::checkFeed(detached, {root, sig::kMinSerial}, r.firmwareSha256), sig::kEnforce);
  return parser.complete() &&
         sig::accepted(sig::checkStaged(sig::kRootHex, sig::kMinSerial, r.tagName, r.firmwareUrl, r.firmwareSha256,
                                        signature, "firmware-x4-pro", digest, r.firmwareSize, r.firmwareSize),
                       sig::kEnforce);
}
