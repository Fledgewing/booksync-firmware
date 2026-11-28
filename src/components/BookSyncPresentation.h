// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <GfxRenderer.h>

#include "TouchActionButtons.h"

// Presentation only: callers supply translated labels and cached values. No
// Library/device-status reads, storage, allocations, or second framebuffer.
namespace BookSyncPresentation {
enum class Type : uint8_t { Title, Body, Label, Value, Caption, Section, Brand };
struct Frame {
  Rect area;
  bool touch;
  bool textDevice;
  int unit(int value) const {  // Keep the approved type/control scale on the wider X3 viewport; extra
    // width is available to text rather than increasing every vertical gap.
    const int width = area.width < 480 ? area.width : 480;
    return (value * width + 240) / 480;
  }
  Rect content() const;
  Rect header() const;
  Rect back() const;
  Rect icon() const;
  Rect footer() const;
  Rect row(int y, int height = 62) const;
  TouchActionButtons::Layout actions(int y, uint8_t count, bool stacked = true) const;
};
static_assert(sizeof(Frame) <= 24);
static_assert(sizeof(TouchActionButtons::Layout) <= 96);
Frame frame(const GfxRenderer& renderer, bool touch, bool textDevice);
int font(Type type, bool textDevice = false);
// Single-line labels ellipsize at a UTF-8 boundary. Paragraphs wrap into the
// supplied container. Both use a bounded 128-byte stack scratch buffer.
void text(const GfxRenderer& renderer, const Frame& frame, Rect container, const char* label, Type type,
          bool centered = false, bool black = true);
int paragraph(const GfxRenderer& renderer, const Frame& frame, Rect container, const char* label,
              Type type = Type::Body);
int paragraphHeight(const GfxRenderer& renderer, const Frame& frame, int width, const char* label,
                    Type type = Type::Body);
void header(const GfxRenderer& renderer, const Frame& frame, const char* subtitle, const char* iconLabel = nullptr);
void footer(const GfxRenderer& renderer, const Frame& frame, const char* screen);
void section(const GfxRenderer& renderer, const Frame& frame, Rect container, const char* label);
void row(const GfxRenderer& renderer, const Frame& frame, Rect bounds, const char* label, const char* value,
         bool focused = false, bool chevron = false);
void actions(const GfxRenderer& renderer, const Frame& frame, const TouchActionButtons::Layout& layout,
             const char* const* labels, int primary, int focused);
}  // namespace BookSyncPresentation
