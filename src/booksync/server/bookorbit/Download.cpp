// SPDX-License-Identifier: GPL-3.0-or-later
#include "Download.h"

#include <Logging.h>

#include <algorithm>
#include <cstring>
#if defined(ARDUINO) && !defined(SIMULATOR)
#include <mbedtls/md5.h>
#else
#include <openssl/md5.h>
#endif
#include <limits>
namespace booksync::server {
namespace {
bool digest(const std::array<char, 65>& text, uint8_t* out) {
  if (text[32] || std::memchr(text.data(), 0, 32)) return false;
  auto hex = [](char c) {
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                  : -1;
  };
  for (unsigned i = 0; i < 16; ++i) {
    int a = hex(text[2 * i]), b = hex(text[2 * i + 1]);
    if (a < 0 || b < 0) return false;
    out[i] = uint8_t(a * 16 + b);
  }
  return true;
}
bool hasRevision(const storage::Revision& revision) {
  for (auto byte : revision.bytes)
    if (byte) return true;
  return false;
}
bool proofMatches(const DownloadSpec& spec, const DownloadTarget& target) {
  if (spec.version != target.version) return false;
  if (!spec.proof[0] && !target.proof[0]) return true;
  uint8_t a[16], b[16];
  return digest(spec.proof, a) && digest(target.proof, b) && !std::memcmp(a, b, 16);
}
struct ReaderDigest {
#if defined(ARDUINO) && !defined(SIMULATOR)
  mbedtls_md5_context state;
  ReaderDigest() { mbedtls_md5_init(&state); }
  ~ReaderDigest() { mbedtls_md5_free(&state); }
  bool begin() { return mbedtls_md5_starts(&state) == 0; }
  bool add(const uint8_t* p, size_t n) { return mbedtls_md5_update(&state, p, n) == 0; }
  bool finish(uint8_t* out) { return mbedtls_md5_finish(&state, out) == 0; }
#else
  MD5_CTX state{};
  bool begin() { return MD5_Init(&state) == 1; }
  bool add(const uint8_t* p, size_t n) { return MD5_Update(&state, p, n) == 1; }
  bool finish(uint8_t* out) { return MD5_Final(out, &state) == 1; }
#endif
};
bool strong(const char* value, size_t capacity) {
  const auto* end = static_cast<const char*>(std::memchr(value, 0, capacity));
  if (!end || end - value < 2 || value[0] != '"' || end[-1] != '"') return false;
  for (auto* p = value + 1; p < end - 1; ++p)
    if (static_cast<unsigned char>(*p) < 0x21 || *p == '"' || *p == 0x7f) return false;
  return true;
}
bool number(const char*& p, uint64_t& value) {
  if (*p < '0' || *p > '9') return false;
  value = 0;
  while (*p >= '0' && *p <= '9') {
    unsigned digit = *p++ - '0';
    if (value > (std::numeric_limits<uint64_t>::max() - digit) / 10) return false;
    value = value * 10 + digit;
  }
  return true;
}
bool range(const char* p, uint64_t& first, uint64_t& last, uint64_t& total) {
  if (std::strncmp(p, "bytes ", 6)) return false;
  p += 6;
  if (!number(p, first) || *p++ != '-' || !number(p, last) || *p++ != '/' || !number(p, total) || *p) return false;
  return first <= last && last < total;
}
}  // namespace
bool Download::open(const DownloadSpec& spec, uint64_t offset, const char* validator, DownloadReply& reply) {
  close();
  target_ = {};
  headers_ = {};
  reply = {};
  received_ = 0;
  error_ = DownloadError::Unavailable;
  const bool resolved = transport_.resolve(spec, target_);
  // Metadata resolution can own a client even on failure. Release it before
  // opening the body; close never clears the captured account credentials.
  transport_.close();
  if (!resolved) return false;
  if (target_.identity.book != spec.identity.book || target_.identity.edition != spec.identity.edition ||
      (hasRevision(target_.identity.revision) && hasRevision(spec.identity.revision) &&
       target_.identity.revision != spec.identity.revision) ||
      !proofMatches(spec, target_) || !std::memchr(target_.path.data(), 0, target_.path.size())) {
    error_ = DownloadError::Edition;
    LOG_ERR("BSD", "Download exact selection unavailable");
    return false;
  }
  const char* path = target_.path.data();
  // Only same-origin relative acquisition. No scheme, authority, fragments,
  // control characters or backslash normalization ambiguity crosses this port.
  if (path[0] != '/' || path[1] == '/' || std::strchr(path, '\\') || std::strchr(path, '#')) return false;
  for (const auto* p = path; *p; ++p)
    if (static_cast<unsigned char>(*p) <= 0x20 || *p == 0x7f) return false;
  // A fully retained, worker-hashed staging file needs fresh exact-selection
  // authorization, not an impossible Range starting one byte past EOF.
  if (spec.sizeKnown && offset && offset == spec.bytes && strong(validator, 128)) {
    metadataOnly_ = true;
    reply.identity = spec.identity;
    reply.offset = offset;
    reply.bytes = spec.bytes;
    reply.sizeKnown = true;
    std::memcpy(reply.validator.data(), validator, std::strlen(validator) + 1);
    return true;
  }
  if (offset && !strong(validator, 128)) offset = 0;
  opened_ = true;  // Failed opens may still own a client.
  if (!transport_.open(path, offset, offset ? validator : "", headers_)) {
    close();
    return false;
  }
  bool ok = headers_.status == 200 || headers_.status == 206;
  uint64_t first = 0, last = 0, total = 0;
  if (headers_.status == 206) {
    ok = ok && offset && std::memchr(headers_.contentRange.data(), 0, headers_.contentRange.size()) &&
         range(headers_.contentRange.data(), first, last, total) && first == offset &&
         strong(headers_.etag.data(), headers_.etag.size()) && !std::strcmp(headers_.etag.data(), validator) &&
         last == total - 1 && (!headers_.lengthKnown || headers_.contentLength == last - first + 1);
    known_ = true;
    expected_ = ok ? last - first + 1 : 0;
    reply.offset = first;
    reply.bytes = total;
    reply.sizeKnown = true;
  } else {
    known_ = headers_.lengthKnown;
    expected_ = headers_.contentLength;
    reply.offset = 0;
    reply.bytes = expected_;
    reply.sizeKnown = known_;
  }
  if (!ok) {
    LOG_ERR("BSD", "Download HTTP/range rejected");
    close();
    return false;
  }
  reply.identity = spec.identity;
  if (strong(headers_.etag.data(), headers_.etag.size())) reply.validator = headers_.etag;
  return true;
}
bool Download::verify(FsFile& file, const DownloadSpec& spec, DownloadCheck check) {
  uint8_t expected[16], actual[16];
  if (!check.bytes || check.capacity < 1024 || !digest(spec.proof, expected)) return false;
  ReaderDigest hash;
  if (!hash.begin()) return false;
  // Exactly the companion/reader digest: first 1 KiB, then 1 KiB at offsets
  // 1024*4^i (i=0..10). At most 12 bounded reads; never a whole-file allocation.
  for (unsigned i = 0; i < 12; ++i) {
    const uint64_t offset = i ? 1024ull << (2 * (i - 1)) : 0;
    if (offset >= file.fileSize()) break;
    if (check.permitted && !check.permitted(check.context)) return false;
    const auto n = static_cast<size_t>(std::min<uint64_t>(1024, file.fileSize() - offset));
    if (!file.seekSet(offset) || file.read(check.bytes, n) != static_cast<int>(n) || !hash.add(check.bytes, n))
      return false;
  }
  return hash.finish(actual) && !std::memcmp(actual, expected, 16);
}
int Download::read(void* bytes, size_t capacity) {
  if (metadataOnly_) return 0;
  if (!opened_) return -1;
  const int n = transport_.read(bytes, capacity);
  if (n < 0 || n > int(capacity) || (known_ && (received_ > expected_ || uint64_t(n) > expected_ - received_)))
    return -1;
  received_ += n;
  return n;
}
bool Download::finish() {
  return metadataOnly_ || (opened_ && (!known_ || received_ == expected_) && transport_.finish());
}
void Download::close() {
  if (opened_) transport_.close();
  opened_ = false;
  metadataOnly_ = false;
}
}  // namespace booksync::server
extern "C" bool booksync_download_adapter_link_anchor(booksync::server::Download& source,
                                                      const booksync::DownloadSpec& spec,
                                                      booksync::DownloadReply& reply, void* bytes, FsFile& artifact) {
  const bool ok = source.open(spec, 0, "", reply) && source.read(bytes, 1) >= 0 && source.finish();
  const bool verified = source.verify(artifact, spec, {static_cast<uint8_t*>(bytes), 1024, nullptr, nullptr});
  source.close();
  return ok && verified;
}
