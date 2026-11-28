// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace booksync {
// Worker-only alias plus opaque artwork revision from the published catalog.
// This revision describes artwork, independently of a downloaded EPUB digest.
// References/credentials/expiring acquisition paths remain inside the adapter.
struct CoverSpec {
  std::array<char, 64> server{}, account{}, book{};
  std::array<uint8_t, 32> revision{};
  bool operator==(const CoverSpec&) const = default;
};
struct CoverReply {
  CoverSpec selection{};
  uint64_t bytes = 0;
  bool sizeKnown = false;
};
class CoverSource {
 public:
  virtual ~CoverSource() = default;
  virtual bool open(const CoverSpec&, CoverReply&) = 0;
  virtual int read(void*, size_t capacity) = 0;
  virtual bool finish() = 0;
  virtual void close() = 0;
};
}  // namespace booksync
