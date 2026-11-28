// SPDX-License-Identifier: GPL-3.0-or-later
#include "SourceAuthority.h"

#include <Logging.h>

#include <cstring>
namespace booksync::storage {
namespace {
bool scopeText(std::string_view value) {
  return !value.empty() && value.size() <= 128 && value.find('\0') == value.npos;
}
uint32_t checksum(const char* bytes) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < 330; ++i) hash = (hash ^ uint8_t(bytes[i])) * 16777619u;
  return hash;
}
uint32_t get(const char* p) {
  uint32_t value = 0;
  for (unsigned i = 0; i < 4; ++i) value |= uint32_t(uint8_t(p[i])) << (8 * i);
  return value;
}
bool equal(const char* bytes, std::string_view value) {
  if (memcmp(bytes, value.data(), value.size()) || bytes[value.size()]) return false;
  for (size_t i = value.size() + 1; i < 129; ++i)
    if (bytes[i]) return false;
  return true;
}
}  // namespace
bool SourceAuthority::validate(FsFile& file, void* context) {
  auto& self = *static_cast<SourceAuthority*>(context);
  auto& b = self.bytes_;
  if (file.fileSize() != b.size() || file.read(b.data(), b.size()) != int(b.size()) ||
      memcmp(b.data(), "BSID\1\0\0\0", 8) || get(b.data() + 330) != checksum(b.data()) ||
      !equal(b.data() + 72, self.expected_.server) || !equal(b.data() + 201, self.expected_.account) ||
      memcmp(b.data() + 40, self.revision_.bytes.data(), 32))
    return false;
  SourceIdentity value;
  memcpy(value.book.bytes.data(), b.data() + 8, 16);
  memcpy(value.edition.bytes.data(), b.data() + 24, 16);
  value.revision = self.revision_;
  return valid(value);
}
bool SourceAuthority::load(ScopeView scope, const Revision& revision, SourceIdentity& output) {
  if (!scopeText(scope.server) || !scopeText(scope.account)) return false;
  expected_ = scope;
  revision_ = revision;
  // Never roll an authority back or repair unknown bytes from a backup. An
  // unreadable current record cannot issue different IDs for the same source.
  if (!FilePublish::validateCurrent(paths_, validate, this)) return false;
  SourceIdentity value;
  memcpy(value.book.bytes.data(), bytes_.data() + 8, 16);
  memcpy(value.edition.bytes.data(), bytes_.data() + 24, 16);
  value.revision = revision;
  output = value;
  return true;
}
bool SourceAuthority::issue(ScopeView scope, const Revision& revision, Entropy entropy, void* context,
                            SourceIdentity& output) {
  if (load(scope, revision, output)) return true;
  if (!scopeText(scope.server) || !scopeText(scope.account) || !entropy || Storage.exists(paths_.current) ||
      Storage.exists(paths_.backup)) {
    LOG_ERR("BSID", "Source identity unavailable; authority retained");
    return false;
  }
  bytes_.fill(0);
  memcpy(bytes_.data(), "BSID\1", 5);
  if (!entropy(reinterpret_cast<uint8_t*>(bytes_.data() + 8), 32, context)) {
    LOG_ERR("BSID", "Cannot issue independent source UUIDs");
    return false;
  }
  bool nonzero = false;
  for (size_t i = 8; i < 40; ++i) nonzero = nonzero || bytes_[i];
  if (!nonzero || !memcmp(bytes_.data() + 8, bytes_.data() + 24, 16)) {
    LOG_ERR("BSID", "Independent source UUID entropy is invalid");
    return false;
  }
  // UUID version/variant bits; entropy, not a path, hash or backend ID, supplies
  // the remaining 122 bits of each independently issued identifier.
  for (const size_t start : {size_t(8), size_t(24)}) {
    bytes_[start + 6] = char((uint8_t(bytes_[start + 6]) & 15) | 64);
    bytes_[start + 8] = char((uint8_t(bytes_[start + 8]) & 63) | 128);
  }
  if (!memcmp(bytes_.data() + 8, bytes_.data() + 24, 16)) {
    LOG_ERR("BSID", "Issued source UUIDs collide after version encoding");
    return false;
  }
  memcpy(bytes_.data() + 40, revision.bytes.data(), 32);
  memcpy(bytes_.data() + 72, scope.server.data(), scope.server.size());
  memcpy(bytes_.data() + 201, scope.account.data(), scope.account.size());
  const uint32_t hash = checksum(bytes_.data());
  for (unsigned i = 0; i < 4; ++i) bytes_[330 + i] = char(hash >> (8 * i));
  FilePublish publication(paths_, validate, this);
  if (!publication.begin(bytes_.size()) || !publication.write(bytes_.data(), bytes_.size()) ||
      (!publication.finishNew() && !publication.confirm()) || !load(scope, revision, output)) {
    LOG_ERR("BSID", "Source identity publication incomplete");
    return false;
  }
  return true;
}
}  // namespace booksync::storage
