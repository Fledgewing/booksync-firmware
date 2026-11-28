// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
namespace booksync {
constexpr uint16_t CoverWidth = 128, CoverHeight = 192;
constexpr uint32_t CoverBytes = 3072;
// Opaque, process-lifetime reference. No path, adapter ID or job pointer reaches UI.
struct CoverReference {
  uint64_t id = 0;
  uint16_t width = 0, height = 0;
  bool operator==(CoverReference other) const {
    return id == other.id ? (width == other.width ? height == other.height : false) : false;
  }
};
// Packed rows, MSB first, 1=white; stride is (width+7)/8. Cached copies only.
struct CoverBitmap {
  std::array<uint8_t, CoverBytes> pixels{};
  uint16_t width = 0, height = 0;
};
}  // namespace booksync
