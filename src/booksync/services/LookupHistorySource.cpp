// SPDX-License-Identifier: GPL-3.0-or-later
#include "LookupHistorySource.h"
#include <HalStorage.h>
#include <Logging.h>
#include <cstring>
namespace booksync {
void LookupDecoder::reset() { used_ = 0; offset_ = lineOffset_ = 0; }
LookupStep LookupDecoder::validateWord(std::string_view text) {
  unsigned characters = 0;
  for (size_t i = 0; i < text.size();) {
    if (++characters > 96) return LookupStep::Oversize;
    const auto first = static_cast<uint8_t>(text[i++]);
    if (first < 32 || first == 127) return LookupStep::Malformed;
    if (first < 128) continue;
    unsigned count = 0, codepoint = 0, minimum = 0;
    if (first >= 0xc2 && first <= 0xdf) { count = 1; codepoint = first & 31; minimum = 128; }
    else if (first >= 0xe0 && first <= 0xef) { count = 2; codepoint = first & 15; minimum = 2048; }
    else if (first >= 0xf0 && first <= 0xf4) { count = 3; codepoint = first & 7; minimum = 65536; }
    else return LookupStep::Malformed;
    if (count > text.size() - i) return LookupStep::Malformed;
    while (count--) {
      const auto next = static_cast<uint8_t>(text[i++]);
      if ((next & 0xc0) != 0x80) return LookupStep::Malformed;
      codepoint = (codepoint << 6) | (next & 63);
    }
    if (codepoint < minimum || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) return LookupStep::Malformed;
  }
  return LookupStep::Record;
}
LookupStep LookupDecoder::push(uint8_t byte, LookupEntry& out) {
  ++offset_;
  if (byte != '\n' && byte != '\r') {
    if (used_ == line_.size()) return LookupStep::Oversize;
    line_[used_++] = static_cast<char>(byte);
    return LookupStep::Pending;
  }
  const uint32_t start = lineOffset_;
  lineOffset_ = offset_;
  const auto used = used_;
  used_ = 0;
  if (!used) return LookupStep::EmptyLine;
  const unsigned length = used;
  unsigned separator = length;
  for (unsigned i = length; i; --i) if (line_[i - 1] == '|') { separator = i - 1; break; }
  if (!separator || separator + 2 != length) return LookupStep::Malformed;
  if (separator > 384) return LookupStep::Oversize;
  const char status = line_[separator + 1];
  if (status != 'D' && status != 'T' && status != 'Y' && status != 'S' && status != 'X') return LookupStep::Malformed;
  const auto validity = validateWord({line_.data(), separator});
  if (validity != LookupStep::Record) return validity;
  out.word.fill(0);
  out.offset = 0;
  out.outcome = 0;
  std::memcpy(out.word.data(), line_.data(), separator);
  out.offset = start;
  out.outcome = status;
  return LookupStep::Record;
}
LookupStep LookupHistorySource::fail(LookupStep state) {
  state_ = state;
  complete_ = false;
  LOG_ERR("BSW", "Saved lookup capture unavailable (%u); native bytes retained", unsigned(state));
  return state;
}
LookupStep LookupHistorySource::begin(std::string_view cachePath) {
  decoder_.reset(); frozen_ = loaded_ = 0; pos_ = size_ = 0; complete_ = backup_ = false;
  state_ = LookupStep::Missing;
  if (cachePath.empty() || cachePath.find('\0') != cachePath.npos)
    return fail(LookupStep::Malformed);
  if (cachePath.size() > 1024) return fail(LookupStep::Oversize);
  static constexpr char suffix[] = "/dictionary_history.txt";
  std::memcpy(path_.data(), cachePath.data(), cachePath.size());
  std::memcpy(path_.data() + cachePath.size(), suffix, sizeof(suffix));
  if (!Storage.exists(path_.data())) {
    const auto length = cachePath.size() + sizeof(suffix) - 1;
    std::memcpy(path_.data() + length, ".bak", 5);
    if (!Storage.exists(path_.data())) return state_;
    backup_ = true;
  }
  FsFile file = Storage.open(path_.data(), O_RDONLY);
  if (!file) return fail(LookupStep::IoError);
  const auto bytes = file.fileSize();
  const bool closed = file.close();
  if (!closed) return fail(LookupStep::IoError);
  if (bytes > MaxBytes) return fail(LookupStep::Oversize);
  frozen_ = static_cast<uint32_t>(bytes);
  state_ = LookupStep::Pending;
  return state_;
}
LookupStep LookupHistorySource::tick(LookupEntry& out, uint16_t allowance) {
  if (state_ != LookupStep::Pending) return state_;
  if (!allowance) return LookupStep::Pending;
  if (allowance > BatchBytes) allowance = BatchBytes;
  while (allowance--) {
    if (pos_ == size_) {
      if (loaded_ == frozen_) {
        complete_ = !decoder_.partial();
        state_ = LookupStep::End;
        return state_;
      }
      FsFile file = Storage.open(path_.data(), O_RDONLY);
      if (!file) return fail(LookupStep::IoError);
      const auto remaining = frozen_ - loaded_;
      const auto count = remaining < BatchBytes ? remaining : BatchBytes;
      const bool unchanged = file.fileSize() == frozen_;
      const bool read = unchanged && file.seekSet(loaded_) && file.read(block_.data(), count) == static_cast<int>(count);
      const bool closed = file.close();
      if (!closed || (unchanged && !read)) return fail(LookupStep::IoError);
      if (!unchanged) return fail(LookupStep::Changed);
      loaded_ += count; pos_ = 0; size_ = count;
    }
    const auto step = decoder_.push(block_[pos_++], out);
    if (step == LookupStep::Malformed || step == LookupStep::Oversize) return fail(step);
    if (step == LookupStep::Record || step == LookupStep::EmptyLine) return step;
  }
  return LookupStep::Pending;
}
}  // namespace booksync
// Retain the complete dormant production seam in all physical reserve checks.
extern "C" bool booksync_lookup_capture_link(booksync::LookupHistorySource* source, const char* cache,
                                             booksync::LookupEntry* out, bool begin) {
  if (!source || !cache || !out) return false;
  if (begin) return source->begin(cache) == booksync::LookupStep::Pending;
  return source->tick(*out) == booksync::LookupStep::Record;
}
