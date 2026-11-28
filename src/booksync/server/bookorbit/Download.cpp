// SPDX-License-Identifier: GPL-3.0-or-later
#include "Download.h"

#include <Logging.h>

#include <cstring>
#include <limits>
namespace booksync::server {
namespace {
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
  if (!transport_.resolve(spec, target_) || target_.identity != spec.identity ||
      !std::memchr(target_.path.data(), 0, target_.path.size())) {
    LOG_ERR("BSD", "Download exact selection unavailable");
    return false;
  }
  const char* path = target_.path.data();
  // Only same-origin relative acquisition. No scheme, authority, fragments,
  // control characters or backslash normalization ambiguity crosses this port.
  if (path[0] != '/' || path[1] == '/' || std::strchr(path, '\\') || std::strchr(path, '#')) return false;
  for (const auto* p = path; *p; ++p)
    if (static_cast<unsigned char>(*p) <= 0x20 || *p == 0x7f) return false;
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
  reply.identity = target_.identity;
  if (strong(headers_.etag.data(), headers_.etag.size())) reply.validator = headers_.etag;
  return true;
}
int Download::read(void* bytes, size_t capacity) {
  if (!opened_) return -1;
  const int n = transport_.read(bytes, capacity);
  if (n < 0 || n > int(capacity) || (known_ && (received_ > expected_ || uint64_t(n) > expected_ - received_)))
    return -1;
  received_ += n;
  return n;
}
bool Download::finish() { return opened_ && (!known_ || received_ == expected_) && transport_.finish(); }
void Download::close() {
  if (opened_) transport_.close();
  opened_ = false;
}
}  // namespace booksync::server
extern "C" bool booksync_download_adapter_link_anchor(booksync::server::Download& source,
                                                      const booksync::DownloadSpec& spec,
                                                      booksync::DownloadReply& reply, void* bytes) {
  const bool ok = source.open(spec, 0, "", reply) && source.read(bytes, 1) >= 0 && source.finish();
  source.close();
  return ok;
}
