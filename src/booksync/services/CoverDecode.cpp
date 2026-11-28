// SPDX-License-Identifier: GPL-3.0-or-later
#include "CoverDecode.h"

#include <Logging.h>

#include <algorithm>
namespace booksync {
namespace {
int32_t readJpeg(JPEGFILE* f, uint8_t* bytes, int32_t count) {
  if (count < 0 || f->iPos < 0 || f->iPos > f->iSize) return 0;
  count = std::min(count, f->iSize - f->iPos);
  const int n = static_cast<FsFile*>(f->fHandle)->read(bytes, count);
  if (n < 0 || n > count) return 0;
  f->iPos += n;
  return n;
}
int32_t seekJpeg(JPEGFILE* f, int32_t pos) {
  if (pos < 0 || pos > f->iSize || !static_cast<FsFile*>(f->fHandle)->seekSet(pos)) return -1;
  f->iPos = pos;
  return pos;
}
// Reject unsupported SOF modes and oversized dimensions before JPEGDEC parses
// tables. Segment lengths are checked against the capped file, without scanning
// entropy data or allocating a payload buffer.
bool baseline(FsFile& file, uint32_t size) {
  uint8_t head[6];
  if (file.read(head, 2) != 2 || head[0] != 0xff || head[1] != 0xd8) return false;
  uint32_t pos = 2;
  while (pos + 4 <= size) {
    if (file.read(head, 2) != 2 || head[0] != 0xff) return false;
    pos += 2;
    while (head[1] == 0xff && pos < size) {
      if (file.read(head + 1, 1) != 1) return false;
      ++pos;
    }
    const auto marker = head[1];
    if (marker == 0xda || marker == 0xd9 || marker == 0x00 || marker == 0xd8) return false;
    if (file.read(head, 2) != 2) return false;
    pos += 2;
    const uint32_t length = uint32_t(head[0]) * 256 + head[1];
    if (length < 2 || length - 2 > size - pos) return false;
    if (marker == 0xc0) {
      if (length < 11 || file.read(head, 6) != 6) return false;
      const int h = head[1] * 256 + head[2], w = head[3] * 256 + head[4];
      return head[0] == 8 && (head[5] == 1 || head[5] == 3) && length == uint32_t(8 + 3 * head[5]) && w > 0 && h > 0 &&
             w <= CoverDecode::MaxDimension && h <= CoverDecode::MaxDimension && w <= CoverDecode::MaxPixels / h &&
             file.seekSet(0);
    }
    if (marker >= 0xc1 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc) return false;
    pos += length - 2;
    if (!file.seekSet(pos)) return false;
  }
  return false;
}
// The service owns the single HAL handle and closes it explicitly below.
void closeJpeg(void*) {}
}  // namespace
int CoverDecode::draw(JPEGDRAW* block) {
  auto& self = *static_cast<CoverDecode*>(block->pUser);
  if (self.permitted_ && !self.permitted_(self.context_)) return 0;
  if (!block->pPixels || block->iBpp != 8 || block->iWidth <= 0 || block->iHeight <= 0 || block->x < 0 || block->y < 0)
    return 0;
  auto& out = *self.out_;
  const auto* pixels = reinterpret_cast<const uint8_t*>(block->pPixels);
  const int stride = (out.width + 7) / 8;
  // Destination-driven nearest-neighbor sampling keeps all callback writes
  // within 3072 bytes, including odd MCU edges and narrow source images.
  const int firstY = (block->y * out.height + self.scaledHeight_ - 1) / self.scaledHeight_;
  const int endY = std::min<int>(
      out.height, ((block->y + block->iHeight) * out.height + self.scaledHeight_ - 1) / self.scaledHeight_);
  const int firstX = (block->x * out.width + self.scaledWidth_ - 1) / self.scaledWidth_;
  const int endX = std::min<int>(
      out.width, ((block->x + block->iWidthUsed) * out.width + self.scaledWidth_ - 1) / self.scaledWidth_);
  for (int y = firstY; y < endY; ++y) {
    const int sy = y * self.scaledHeight_ / out.height;
    if (sy < block->y || sy >= block->y + block->iHeight) continue;
    for (int x = firstX; x < endX; ++x) {
      const int sx = x * self.scaledWidth_ / out.width;
      if (sx < block->x || sx >= block->x + block->iWidthUsed) continue;
      const auto gray = pixels[(sy - block->y) * block->iWidth + sx - block->x];
      if (gray < 128) out.pixels[y * stride + x / 8] &= uint8_t(~(0x80 >> (x % 8)));
    }
  }
  return 1;
}
bool CoverDecode::decode(const char* path, CoverBitmap& out, bool (*permitted)(void*), void* context) {
  out = {};
  FsFile file = Storage.open(path, O_RDONLY);
  if (!file) {
    LOG_ERR("BSC", "Cover input open failed");
    return false;
  }
  const auto size = file.fileSize();
  if (!size || size > MaxInput || !baseline(file, static_cast<uint32_t>(size))) {
    file.close();
    LOG_ERR("BSC", "Cover input/header rejected");
    return false;
  }
  const bool opened = decoder_.open(&file, static_cast<int>(size), closeJpeg, readJpeg, seekJpeg, draw);
  bool ok = false;
  if (opened) {
    const int w = decoder_.getWidth(), h = decoder_.getHeight();
    if (decoder_.getJPEGType() == JPEG_MODE_BASELINE && w > 0 && h > 0 && w <= MaxDimension && h <= MaxDimension &&
        w <= MaxPixels / h && (!permitted || permitted(context))) {
      const int divisor = std::max({1, (w + CoverWidth - 1) / CoverWidth, (h + CoverHeight - 1) / CoverHeight});
      out.width = std::max(1, w / divisor);
      out.height = std::max(1, h / divisor);
      out.pixels.fill(0xff);
      int scale = 1, options = 0;
      if (divisor >= 8) {
        scale = 8;
        options = JPEG_SCALE_EIGHTH;
      } else if (divisor >= 4) {
        scale = 4;
        options = JPEG_SCALE_QUARTER;
      } else if (divisor >= 2) {
        scale = 2;
        options = JPEG_SCALE_HALF;
      }
      scaledWidth_ = std::max(1, w / scale);
      scaledHeight_ = std::max(1, h / scale);
      out_ = &out;
      permitted_ = permitted;
      context_ = context;
      decoder_.setUserPointer(this);
      decoder_.setPixelType(EIGHT_BIT_GRAYSCALE);
      decoder_.setMaxOutputSize(1);
      ok = decoder_.decode(0, 0, options) != 0 && (!permitted || permitted(context));
    }
    decoder_.close();
  }
  const bool closed = file.close();
  out_ = nullptr;
  permitted_ = nullptr;
  context_ = nullptr;
  if (!ok || !closed) {
    out = {};
    LOG_ERR("BSC", "Cover decode rejected");
    return false;
  }
  return true;
}
}  // namespace booksync
