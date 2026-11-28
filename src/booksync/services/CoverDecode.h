// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <HalStorage.h>
#include <JPEGDEC.h>

#include "booksync/interface/Covers.h"
namespace booksync {
// One reusable decoder workspace owned by the cover service, never task stack.
// JPEG baseline only; progressive/lossless images are outside this thumbnail contract.
class CoverDecode {
 public:
  static constexpr uint32_t MaxInput = 512 * 1024;
  static constexpr int MaxDimension = 2048, MaxPixels = 2 * 1024 * 1024;
  bool decode(const char* path, CoverBitmap& out, bool (*permitted)(void*) = nullptr, void* context = nullptr);

 private:
  static int draw(JPEGDRAW*);
  JPEGDEC decoder_{};
  CoverBitmap* out_ = nullptr;
  bool (*permitted_)(void*) = nullptr;
  void* context_ = nullptr;
  int scaledWidth_ = 0, scaledHeight_ = 0;
};
static_assert(sizeof(CoverDecode) <= 24 * 1024);
}  // namespace booksync
