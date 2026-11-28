// SPDX-License-Identifier: GPL-3.0-or-later
#include <Logging.h>
#include <Memory.h>

#include <cstring>

#include "Capture.h"

namespace booksync::reader {
namespace {
constexpr size_t Header = 80, MaxProof = 1152;
bool text(std::string_view value, size_t bound) {
  return !value.empty() && value.size() <= bound && value.find('\0') == value.npos;
}
void put(char* p, size_t n) {
  p[0] = static_cast<char>(n);
  p[1] = static_cast<char>(n >> 8);
}
uint16_t get(const char* p) { return uint8_t(p[0]) | (uint16_t(uint8_t(p[1])) << 8); }
}  // namespace
storage::JournalResult Capture::open(std::string_view path, uint16_t format, const CaptureBinding& binding) {
  using storage::JournalResult;
  if (id_ || !format || !text(path, 1024) || !storage::valid(binding.source) || !text(binding.scope.server, 128) ||
      !text(binding.scope.account, 128) || binding.proof.empty() || binding.proof.size() > MaxProof)
    return JournalResult::Invalid;
  const size_t size =
      Header + path.size() + binding.scope.server.size() + binding.scope.account.size() + binding.proof.size();
  // At most 2512 bytes for this cold open transaction; too large for the C3
  // stack, and not retained as permanent DRAM or allocated in paint/page loops.
  auto bytes = makeUniqueNoThrow<char[]>(size);
  if (!bytes) {
    LOG_ERR("BSC", "Cannot allocate bound native capture: %u", unsigned(size));
    return JournalResult::IoError;
  }
  memset(bytes.get(), 0, Header);
  memcpy(bytes.get(), "BSO2", 4);
  put(bytes.get() + 4, format);
  put(bytes.get() + 6, path.size());
  put(bytes.get() + 8, binding.scope.server.size());
  put(bytes.get() + 10, binding.scope.account.size());
  put(bytes.get() + 12, binding.proof.size());
  memcpy(bytes.get() + 16, binding.source.book.bytes.data(), 16);
  memcpy(bytes.get() + 32, binding.source.edition.bytes.data(), 16);
  memcpy(bytes.get() + 48, binding.source.revision.bytes.data(), 32);
  size_t cursor = Header;
  for (auto value : {path, binding.scope.server, binding.scope.account, binding.proof}) {
    memcpy(bytes.get() + cursor, value.data(), value.size());
    cursor += value.size();
  }
  const auto result =
      journal_.enqueue({0, "native", "local-unbound", "native-source", {bytes.get(), size}, {}, 0}, id_);
  if (result == JournalResult::Ok) {
    bound_ = true;
    if (captureChanged) captureChanged();
  }
  return result;
}
bool Capture::decodeOpen(std::string_view payload, OpenCapture& opened) {
  if (payload.size() < 8) return false;
  OpenCapture value;
  value.format = get(payload.data() + 4);
  const size_t pathBytes = get(payload.data() + 6);
  if (!value.format || !pathBytes || pathBytes > 1024) return false;
  if (payload.substr(0, 4) == "BSO1") {
    if (payload.size() != 8 + pathBytes) return false;
    value.path = payload.substr(8);
  } else if (payload.substr(0, 4) == "BSO2") {
    if (payload.size() < Header || payload[14] || payload[15]) return false;
    const size_t serverBytes = get(payload.data() + 8), accountBytes = get(payload.data() + 10),
                 proofBytes = get(payload.data() + 12);
    if (!serverBytes || serverBytes > 128 || !accountBytes || accountBytes > 128 || !proofBytes ||
        proofBytes > MaxProof || payload.size() != Header + pathBytes + serverBytes + accountBytes + proofBytes)
      return false;
    memcpy(value.binding.source.book.bytes.data(), payload.data() + 16, 16);
    memcpy(value.binding.source.edition.bytes.data(), payload.data() + 32, 16);
    memcpy(value.binding.source.revision.bytes.data(), payload.data() + 48, 32);
    if (!storage::valid(value.binding.source)) return false;
    value.path = payload.substr(Header, pathBytes);
    value.binding.scope.server = payload.substr(Header + pathBytes, serverBytes);
    value.binding.scope.account = payload.substr(Header + pathBytes + serverBytes, accountBytes);
    value.binding.proof = payload.substr(Header + pathBytes + serverBytes + accountBytes);
    if (!text(value.binding.scope.server, 128) || !text(value.binding.scope.account, 128)) return false;
    value.bound = true;
  } else
    return false;
  if (!text(value.path, 1024)) return false;
  opened = value;
  return true;
}
}  // namespace booksync::reader
