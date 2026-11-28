// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "booksync/interface/Covers.h"
namespace approvedWidgetCovers {
// Simulator-only prepared fixture cache. Files are loaded before Home enters;
// readCover then copies fixed cached pixels, including forced paint probes.
inline booksync::CoverBitmap bitmaps[9];
inline bool loaded[9]{};
inline void load(uint8_t first, uint8_t count) {
  constexpr const char* names[] = {"continue",   "book-0",     "book-1",     "book-2",    "book-3",
                                   "personal-0", "personal-1", "personal-2", "personal-3"};
  const char* directory = std::getenv("BOOKSYNC_APPROVED_COVERS");
  if (!directory) {
    LOG_ERR("BSWC", "Approved cover fixture directory missing");
    std::_Exit(131);
  }
  for (uint8_t i = first; i < first + count && i < 9; ++i) {
    if (loaded[i]) continue;
    // Reused simulator startup path; no task-stack path buffer.
    static char path[1024];
    const int length = std::snprintf(path, sizeof(path), "%s/%s.1bpp", directory, names[i]);
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(path)) std::_Exit(132);
    auto* file = std::fopen(path, "rb");
    if (!file) {
      LOG_ERR("BSWC", "Approved cover fixture missing");
      std::_Exit(133);
    }
    auto& bitmap = bitmaps[i];
    bitmap.width = 128;
    bitmap.height = i ? 180 : 192;
    bitmap.pixels.fill(0xff);
    const size_t bytes = bitmap.width * bitmap.height / 8;
    const auto read = std::fread(bitmap.pixels.data(), 1, bytes, file);
    std::fclose(file);
    if (read != bytes) {
      LOG_ERR("BSWC", "Approved cover fixture truncated");
      std::_Exit(134);
    }
    loaded[i] = true;
  }
}
inline bool copy(uint8_t index, booksync::CoverBitmap& out) {
  if (index >= 9 || !loaded[index]) return false;
  const auto& bitmap = bitmaps[index];
  out.width = bitmap.width;
  out.height = bitmap.height;
  std::memcpy(out.pixels.data(), bitmap.pixels.data(), out.pixels.size());
  return true;
}
}  // namespace approvedWidgetCovers
