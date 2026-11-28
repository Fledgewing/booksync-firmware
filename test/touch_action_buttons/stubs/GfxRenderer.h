#pragma once

#include <EpdFontFamily.h>

enum class Color { Black };
class GfxRenderer {
 public:
  void fillRoundedRect(int, int, int, int, int, Color) const {}
  void drawRoundedRect(int, int, int, int, int, int, bool) const {}
  void beginTextClip(int, int, int, int) const {}
  void endTextClip() const {}
  int getLineHeight(int) const { return 20; }
  int getTextWidth(int, const char*, EpdFontFamily::Style) const { return 0; }
  void fillRect(int, int, int, int, bool) const {}
  void drawRect(int, int, int, int, bool) const {}
  void drawText(int, int, int, const char*, bool, EpdFontFamily::Style) const {}
};
