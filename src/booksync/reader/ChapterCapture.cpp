// SPDX-License-Identifier: GPL-3.0-or-later
#include "ChapterCapture.h"

#include <Logging.h>

#include <cstring>
namespace booksync::reader {
namespace {
bool copyLabel(std::string_view text, std::array<char, 96>& out, bool& truncated) {
  for (size_t i = 0; i < text.size();) {
    const auto first = static_cast<uint8_t>(text[i++]);
    if (first < 32 || first == 127) return false;
    if (first < 128) continue;
    unsigned left = 0, cp = 0, minimum = 0;
    if (first >= 0xc2 && first <= 0xdf) {
      left = 1;
      cp = first & 31;
      minimum = 128;
    } else if (first >= 0xe0 && first <= 0xef) {
      left = 2;
      cp = first & 15;
      minimum = 2048;
    } else if (first >= 0xf0 && first <= 0xf4) {
      left = 3;
      cp = first & 7;
      minimum = 65536;
    } else
      return false;
    if (left > text.size() - i) return false;
    while (left--) {
      const auto next = static_cast<uint8_t>(text[i++]);
      if ((next & 0xc0) != 0x80) return false;
      cp = (cp << 6) | (next & 63);
    }
    if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
  }
  size_t n = text.size();
  if (n >= out.size()) {
    n = out.size() - 1;
    while (n && (static_cast<unsigned char>(text[n]) & 0xc0) == 0x80) --n;
    truncated = true;
  }
  out.fill(0);
  if (n) std::memcpy(out.data(), text.data(), n);
  return true;
}
}  // namespace
void ChapterValue::clear() {
  open = revision = 0;
  book.fill(0);
  current.fill(0);
  next.fill(0);
  toNextSeconds = toEndSeconds = 0;
  chapter = remaining = format = 0;
  state = ChapterState::NotSaved;
  toNextKnown = toEndKnown = labelsTruncated = false;
}
ChapterCapture& chapterCapture() {
  static ChapterCapture value;
  return value;
}
void ChapterCapture::retire() {
  value_.clear();
  value_.revision = ++sequence_;
}
void ChapterCapture::begin(uint64_t open, uint16_t format) {
  retire();
  value_.open = open;
  value_.format = format;
  if (format != 1) value_.state = ChapterState::Unsupported;
}
bool ChapterCapture::fail(ChapterState state) {
  const auto open = value_.open;
  const auto format = value_.format;
  value_.clear();
  value_.open = open;
  value_.format = format;
  value_.state = state;
  value_.revision = ++sequence_;
  LOG_ERR("BSC", "Native chapter observation refused (%u)", unsigned(state));
  return false;
}
bool ChapterCapture::publish(ChapterInput in) {
  if (!in.open || in.open != value_.open || value_.format != 1) return false;
  if (!in.tocKnown) {
    value_.book.fill(0);
    value_.current.fill(0);
    value_.next.fill(0);
    value_.toNextSeconds = value_.toEndSeconds = 0;
    value_.toNextKnown = value_.toEndKnown = false;
    value_.chapter = value_.remaining = 0;
    value_.state = ChapterState::NoContents;
    value_.revision = ++sequence_;
    return true;
  }
  if (in.count > 4096 || in.book.size() > 480 || in.current.size() > 480 || in.next.size() > 480)
    return fail(ChapterState::Oversize);
  if (!in.count || in.chapter >= in.count || in.current.empty() || in.book.empty() ||
      (in.chapter + 1 < in.count && in.next.empty()) || (in.toNextKnown && in.toNextSeconds > 7776000) ||
      (in.toEndKnown && in.toEndSeconds > 7776000))
    return fail(ChapterState::Unavailable);
  value_.labelsTruncated = in.labelsTruncated;
  if (!copyLabel(in.book, value_.book, value_.labelsTruncated) ||
      !copyLabel(in.current, value_.current, value_.labelsTruncated) ||
      !copyLabel(in.next, value_.next, value_.labelsTruncated))
    return fail(ChapterState::Unavailable);
  value_.chapter = static_cast<uint16_t>(in.chapter);
  value_.remaining = static_cast<uint16_t>(in.count - in.chapter - 1);
  value_.toNextKnown = in.toNextKnown;
  value_.toEndKnown = in.toEndKnown;
  value_.toNextSeconds = in.toNextKnown ? in.toNextSeconds : 0;
  value_.toEndSeconds = in.toEndKnown ? in.toEndSeconds : 0;
  value_.state = ChapterState::Ready;
  value_.revision = ++sequence_;
  return true;
}
}  // namespace booksync::reader
