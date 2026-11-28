// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <string_view>
namespace booksync::reader {
enum class ChapterState : uint8_t { NotSaved, Ready, NoContents, Unsupported, Unavailable, Oversize };
struct ChapterInput {
  uint64_t open = 0;
  std::string_view book, current, next;
  uint32_t chapter = 0, count = 0, toNextSeconds = 0, toEndSeconds = 0;
  bool toNextKnown = false, toEndKnown = false, tocKnown = false, labelsTruncated = false;
};
struct ChapterValue {
  uint64_t open = 0, revision = 0;
  std::array<char, 96> book{}, current{}, next{};
  uint32_t toNextSeconds = 0, toEndSeconds = 0;
  uint16_t chapter = 0, remaining = 0, format = 0;
  ChapterState state = ChapterState::NotSaved;
  bool toNextKnown = false, toEndKnown = false, labelsTruncated = false;
  void clear();
};
// Main-task capture only, under the retained RenderLock. The native reader
// supplies its actual TOC and existing pace estimates on exit. No new timer,
// file, parser, account identity or reader navigation policy is introduced.
class ChapterCapture {
 public:
  void begin(uint64_t open, uint16_t format);
  bool publish(ChapterInput);
  void retire();
  const ChapterValue& value() const { return value_; }

 private:
  ChapterValue value_{};
  inline static uint64_t sequence_ = 0;
  bool fail(ChapterState);
};
ChapterCapture& chapterCapture();
static_assert(sizeof(ChapterCapture) <= 352);
}  // namespace booksync::reader
