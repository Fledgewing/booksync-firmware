// SPDX-License-Identifier: GPL-3.0-or-later
#include "Diagnostics.h"

#include <Logging.h>

#include <cstring>

namespace booksync::update {
namespace {
constexpr storage::PublishPaths paths{"/.booksync/update.receipt", "/.booksync/update.receipt.tmp",
                                      "/.booksync/update.receipt.bak"};
constexpr size_t length = 76;
uint32_t checksum(const uint8_t* bytes) {
  uint32_t result = 2166136261u;
  for (size_t i = 0; i < length - 4; ++i) result = (result ^ bytes[i]) * 16777619u;
  return result;
}
void put32(uint8_t* bytes, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) bytes[i] = static_cast<uint8_t>(value >> (8 * i));
}
bool decode(FsFile& file, Receipt* out) {
  uint8_t bytes[length];
  if (file.fileSize() != length || file.read(bytes, length) != length || memcmp(bytes, "BSU1", 4) ||
      bytes[4] > static_cast<uint8_t>(Phase::Returned) || bytes[5] > 1 || bytes[6] || bytes[7])
    return false;
  uint8_t sum[4];
  put32(sum, checksum(bytes));
  if (memcmp(sum, bytes + length - 4, 4)) return false;
  if (out) {
    out->phase = static_cast<Phase>(bytes[4]);
    out->returning = bytes[5];
    memcpy(out->source, bytes + 8, 32);
    memcpy(out->target, bytes + 40, 32);
  }
  return true;
}
}  // namespace
bool Diagnostics::valid(FsFile& file, void* context) { return decode(file, static_cast<Receipt*>(context)); }
bool Diagnostics::load(Receipt& out) {
  out = {};
  if (!storage::FilePublish::validateCurrent(paths, valid, &out)) {
    LOG_ERR("UPDATE", "Update receipt unavailable; boot outcome unproven");
    return false;
  }
  return true;
}
bool Diagnostics::save(const Receipt& value) {
  uint8_t bytes[length]{};
  memcpy(bytes, "BSU1", 4);
  bytes[4] = static_cast<uint8_t>(value.phase);
  bytes[5] = value.returning;
  memcpy(bytes + 8, value.source, 32);
  memcpy(bytes + 40, value.target, 32);
  put32(bytes + length - 4, checksum(bytes));
  storage::FilePublish transaction(paths, valid);
  return transaction.begin(length) && transaction.write(bytes, length) && transaction.finish();
}
}  // namespace booksync::update
