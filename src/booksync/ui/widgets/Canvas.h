// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/Models.h"
namespace booksync::widgets {
enum class Label : uint8_t {
  Continue,
  Reading,
  Resume,
  EmptyContinue,
  NotSaved,
  Off,
  Unsupported,
  Error,
  Retry,
  Progress,
  TimeLeft,
  FinishDayText,
  SavedAge,
  SavedUnknown,
  OversizedData,
  ShelfTitle,
  AllBooks,
  EmptyShelf,
  BookProgress
};
struct Box {
  int x = 0, y = 0, w = 0, h = 0;
};
// Approved gallery: right 7, All min-width 79, 50-pixel height;
// Another is 50 wide, five pixels immediately to its left.
inline Box allHeaderBox(int width) { return {width - 86, 0, 79, 50}; }
inline Box anotherHeaderBox(int width) { return {width - 141, 0, 50, 50}; }
enum class Drawing : uint8_t { Fill, Border, Text, Title, SerifTitle, BookJacket, OnCardMark };
struct PaintCommand {
  Box box{};
  uint64_t first = 0, second = 0;
  const char* caption = nullptr;
  const BookRow* book = nullptr;
  Drawing kind = Drawing::Fill;
  Label label = Label::Continue;
  uint8_t thickness = 1, radius = 0;
  uint16_t scale = 0;
  bool black = true, bold = false;
};
// Activity-owned command scratch, reused for every widget. Borrowed title/row
// spans remain valid for this paint; no graphics, Library, allocation or I/O here.
class Canvas {
 public:
  std::array<PaintCommand, 24> commands{};
  uint8_t count = 0;
  void clear() { count = 0; }
  void fill(Box b, bool black, uint8_t radius = 0) {
    if (auto* c = appendPaint(b, Drawing::Fill, black)) c->radius = radius;
  }
  void border(Box b, bool black, int thickness = 1, uint8_t radius = 0) {
    if (auto* c = appendPaint(b, Drawing::Border, black)) {
      c->thickness = thickness;
      c->radius = radius;
    }
  }
  void onCard(Box b) { appendPaint(b, Drawing::OnCardMark, true); }
  void title(Box b, const char* paintCaption, bool black, bool bold = false, uint16_t scale = 0) {
    if (auto* c = appendPaint(b, Drawing::Title, black)) {
      c->caption = paintCaption;
      c->bold = bold;
      c->scale = scale;
    }
  }
  void serifTitle(Box b, const char* caption, bool black, uint16_t scale = 192) {
    if (auto* c = appendPaint(b, Drawing::SerifTitle, black)) {
      c->caption = caption;
      c->bold = true;
      c->scale = scale;
    }
  }
  void cover(Box b, const BookRow& book, bool black) {
    if (auto* c = appendPaint(b, Drawing::BookJacket, black)) c->book = &book;
  }
  void text(Box b, Label label, bool black, bool bold = false, uint64_t first = 0, uint64_t second = 0) {
    if (auto* c = appendPaint(b, Drawing::Text, black)) {
      c->label = label;
      c->bold = bold;
      c->first = first;
      c->second = second;
    }
  }

 private:
  PaintCommand* appendPaint(Box b, Drawing kind, bool black) {
    if (count == 24) return nullptr;
    auto& c = commands[count++];
    c = {};
    c.box = b;
    c.kind = kind;
    c.black = black;
    return &c;
  }
};
static_assert(sizeof(Canvas) <= 1536);
}  // namespace booksync::widgets
