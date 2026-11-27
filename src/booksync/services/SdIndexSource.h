// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "DeviceIndex.h"
namespace booksync {
// Bounded depth-first walk. Hidden firmware/cache directories are excluded.
// Over-depth/name/path errors fail the generation instead of claiming completeness.
class SdIndexSource {
 public:
  bool start();
  void close();
  IndexSource source() { return {next, this}; }

 private:
  static constexpr unsigned Depth = 8;
  std::array<FsFile, Depth> directories_{};
  std::array<uint16_t, Depth> lengths_{};
  std::array<char, 1025> path_{};
  std::array<char, 256> name_{};
  unsigned depth_ = 0;
  static ScanStep next(void*, storage::IndexRecord&);
};
}  // namespace booksync
