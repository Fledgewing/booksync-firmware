// SPDX-License-Identifier: GPL-3.0-or-later
#include "Cover.h"

#include <Logging.h>

#include <cstring>
namespace booksync::server {
bool Cover::open(const CoverSpec& spec, CoverReply& reply) {
  close();
  target_ = {};
  headers_ = {};
  received_ = 0;
  reply = {};
  const bool resolved = transport_.resolve(spec, target_);
  transport_.close();  // Failed resolution may still own a client.
  if (!resolved || target_.selection != spec || !std::memchr(target_.path.data(), 0, target_.path.size())) {
    LOG_ERR("BSC", "Cover exact selection unavailable");
    return false;
  }
  const char* path = target_.path.data();
  if (path[0] != '/' || path[1] == '/' || std::strchr(path, '\\') || std::strchr(path, '#')) return false;
  for (const char* p = path; *p; ++p)
    if (uint8_t(*p) <= 0x20 || uint8_t(*p) == 0x7f) return false;
  opened_ = true;
  if (!transport_.open(path, headers_) || headers_.status != 200 || !headers_.jpeg) {
    LOG_ERR("BSC", "Cover response rejected");
    close();
    return false;
  }
  reply.selection = spec;
  reply.bytes = headers_.bytes;
  reply.sizeKnown = headers_.sizeKnown;
  return true;
}
int Cover::read(void* out, size_t capacity) {
  if (!opened_) return -1;
  const int n = transport_.read(out, capacity);
  if (n < 0 || size_t(n) > capacity ||
      (headers_.sizeKnown && (received_ > headers_.bytes || uint64_t(n) > headers_.bytes - received_)))
    return -1;
  received_ += n;
  return n;
}
bool Cover::finish() { return opened_ && (!headers_.sizeKnown || received_ == headers_.bytes) && transport_.finish(); }
void Cover::close() {
  if (opened_) transport_.close();
  opened_ = false;
}
}  // namespace booksync::server
extern "C" bool booksync_cover_adapter_link_anchor(booksync::server::Cover& source, const booksync::CoverSpec& spec,
                                                   booksync::CoverReply& reply, void* bytes) {
  const bool ok = source.open(spec, reply) && source.read(bytes, 1) >= 0 && source.finish();
  source.close();
  return ok;
}
