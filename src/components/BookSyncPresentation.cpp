// SPDX-License-Identifier: GPL-3.0-or-later
#include "BookSyncPresentation.h"

#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#ifdef SIMULATOR
#include <cstdlib>
#endif
#include "UITheme.h"
#include "fontIds.h"
namespace BookSyncPresentation {
namespace {
EpdFontFamily::Style weight(Type type) {
  return type == Type::Body || type == Type::Value || type == Type::Caption ? EpdFontFamily::REGULAR
                                                                            : EpdFontFamily::BOLD;
}
// Record actual measured ink before clipping, so the simulator test detects
// overflow instead of hiding it. This instrumentation is absent on devices.
void record(const GfxRenderer& r, Rect box, int id, int x, int y, const char* label, EpdFontFamily::Style style,
            int8_t spacing = 0) {
#ifdef SIMULATOR
  if (const char* path = std::getenv("BOOKSYNC_KIT_GEOMETRY")) {
    if (FILE* f = std::fopen(path, "a")) {
      const auto ink = r.getTextVerticalBounds(id, label, style, BidiUtils::BidiBaseDir::AUTO, spacing);
      std::fprintf(f, "text %d %d %d %d %d %d %d %d\n", box.x, box.y, box.width, box.height, x, y + ink.top,
                   r.getTextWidth(id, label, style, BidiUtils::BidiBaseDir::AUTO, spacing), ink.bottom - ink.top);
      std::fclose(f);
    }
  }
#endif
}
void draw(const GfxRenderer& r, Rect box, int id, int x, int y, const char* label, EpdFontFamily::Style style,
          bool black, int8_t spacing = 0) {
  record(r, box, id, x, y, label, style, spacing);
  r.beginTextClip(box.x, box.y, box.width, box.height);
  r.drawText(id, x, y, label, black, style, BidiUtils::BidiBaseDir::AUTO, spacing);
  r.endTextClip();
}
unsigned prefix(const GfxRenderer& r, int id, const char* label, char (&out)[128], int width,
                EpdFontFamily::Style style, int8_t spacing = 0) {
  unsigned n = 0;
  while (label[n] && n < sizeof(out) - 5) {
    unsigned end = n + 1;
    while (label[end] && (static_cast<unsigned char>(label[end]) & 0xc0) == 0x80) ++end;
    if (end >= sizeof(out) - 4) break;
    std::memcpy(out + n, label + n, end - n);
    out[end] = 0;
    if (r.getTextWidth(id, out, style, BidiUtils::BidiBaseDir::AUTO, spacing) > width) {
      out[n] = 0;
      break;
    }
    n = end;
  }
  out[n] = 0;
  return n;
}
}  // namespace
Rect Frame::header() const { return Rect{area.x, area.y, area.width, unit(58)}; }
Rect Frame::back() const { return Rect{area.x + unit(12), area.y + unit(7), unit(74), unit(44)}; }
Rect Frame::icon() const { return Rect{area.x + area.width - unit(54), area.y + unit(7), unit(44), unit(44)}; }
Rect Frame::footer() const { return Rect{area.x, area.y + area.height - unit(50), area.width, unit(50)}; }
Rect Frame::content() const {
  return Rect{area.x + unit(18), area.y + unit(84), area.width - unit(36), footer().y - area.y - unit(110)};
}
Rect Frame::row(int y, int height) const { return Rect{content().x, y, content().width, unit(height)}; }
TouchActionButtons::Layout Frame::actions(int y, uint8_t count, bool stacked) const {
  const auto c = content();
  const int h = unit(textDevice ? 44 : 50), gap = unit(14);
  const Rect bounds{c.x, y, c.width, stacked ? h * count + gap * (count ? count - 1 : 0) : h};
  return stacked ? TouchActionButtons::vertical(bounds, count, h, gap)
                 : TouchActionButtons::horizontal(bounds, count, gap);
}
Frame frame(const GfxRenderer& r, bool touch, bool textDevice) {
  return {UITheme::getInstance().getScreenSafeArea(r, false, false), touch, textDevice};
}
int font(Type type, bool textDevice) {
  switch (type) {
    case Type::Title:
      return textDevice ? UI_12_FONT_ID : BITTER_14_FONT_ID;
    case Type::Label:
      return BOOKSYNC_LABEL_FONT_ID;
    case Type::Brand:
      return BOOKSYNC_BODY_FONT_ID;
    case Type::Body:
      return BOOKSYNC_BODY_FONT_ID;
    case Type::Value:
      return BOOKSYNC_VALUE_FONT_ID;
    case Type::Caption:
    case Type::Section:
      return BOOKSYNC_CAPTION_FONT_ID;
    default:
      return BOOKSYNC_BODY_FONT_ID;
  }
}
void text(const GfxRenderer& r, const Frame& f, Rect box, const char* label, Type type, bool centered, bool black) {
  if (!label || box.width <= 0 || box.height <= 0) return;
  const int id = font(type, f.textDevice);
  const auto style = weight(type);
  const int8_t spacing = type == Type::Section ? 2 : 0;
  char visible[128];
  unsigned n = prefix(r, id, label, visible, box.width, style, spacing);
  if (label[n]) {
    while (true) {
      std::memcpy(visible + n, "…", 4);
      if (r.getTextWidth(id, visible, style, BidiUtils::BidiBaseDir::AUTO, spacing) <= box.width) break;
      if (!n) {
        visible[0] = 0;
        break;
      }
      do {
        --n;
      } while (n && (static_cast<unsigned char>(visible[n]) & 0xc0) == 0x80);
      visible[n] = 0;
    }
  }
  const auto ink = r.getTextVerticalBounds(id, visible, style, BidiUtils::BidiBaseDir::AUTO, spacing);
  const int x =
      box.x +
      (centered ? (box.width - r.getTextWidth(id, visible, style, BidiUtils::BidiBaseDir::AUTO, spacing)) / 2 : 0);
  const int y = box.y + (box.height - (ink.bottom - ink.top)) / 2 - ink.top;
  draw(r, box, id, x, y, visible, style, black, spacing);
}
static int layoutParagraph(const GfxRenderer& r, const Frame& f, Rect box, const char* label, Type type, bool paint) {
  const int id = font(type, f.textDevice), line = std::max(r.getLineHeight(id), f.unit(type == Type::Title     ? 34
                                                                                       : type == Type::Caption ? 20
                                                                                                               : 26));
  int y = box.y;
  while (*label && y + line <= box.y + box.height) {
    char visible[128];
    unsigned n = prefix(r, id, label, visible, box.width, weight(type), type == Type::Section ? 2 : 0);
    if (!n) break;
    if (label[n]) {
      unsigned word = n;
      while (word && label[word] != ' ') --word;
      if (word) n = word;
    }
    visible[n] = 0;
    if (paint) text(r, f, Rect{box.x, y, box.width, line}, visible, type);
    y += line;
    label += n;
    while (*label == ' ') ++label;
  }
  return y;
}
int paragraph(const GfxRenderer& r, const Frame& f, Rect box, const char* label, Type type) {
  return layoutParagraph(r, f, box, label, type, true);
}
int paragraphHeight(const GfxRenderer& r, const Frame& f, int width, const char* label, Type type) {
  return layoutParagraph(r, f, Rect{0, 0, width, 4096}, label, type, false);
}

void header(const GfxRenderer& r, const Frame& f, const char* subtitle, const char* iconLabel) {
#ifdef SIMULATOR
  if (const char* path = std::getenv("BOOKSYNC_KIT_GEOMETRY")) {
    if (FILE* log = std::fopen(path, "a")) {
      std::fprintf(log, "frame %d %d %d %d\n", f.area.x, f.area.y, f.area.width, f.area.height);
      std::fclose(log);
    }
  }
#endif
  const auto h = f.header(), b = f.back();
  r.drawRoundedRect(b.x, b.y, b.width, b.height, 1, b.height / 2, true);
  text(r, f, b, tr(STR_BACK), Type::Section, true);
  const int x = b.x + b.width + f.unit(14), right = iconLabel ? f.icon().x - f.unit(10) : h.x + h.width - f.unit(12);
  text(r, f, Rect{x, h.y + f.unit(3), right - x, f.unit(28)}, tr(STR_BOOKSYNC_NAME), Type::Brand);
  text(r, f, Rect{x, h.y + f.unit(29), right - x, f.unit(24)}, subtitle, Type::Caption);
  if (iconLabel) {
    const auto i = f.icon();
    r.drawRoundedRect(i.x, i.y, i.width, i.height, 1, i.width / 2, true);
    text(r, f, i, iconLabel, Type::Label, true);
  }
  r.drawLine(h.x, h.y + h.height, h.x + h.width - 1, h.y + h.height);
}
void footer(const GfxRenderer& r, const Frame& f, const char* screen) {
  const auto b = f.footer();
  r.drawLine(b.x, b.y, b.x + b.width - 1, b.y);
  char label[128];
  std::snprintf(label, sizeof(label), tr(STR_BOOKSYNC_SCREEN_FORMAT), screen);
  text(r, f, Rect{b.x + f.unit(18), b.y + f.unit(8), b.width - f.unit(36), b.height - f.unit(16)}, label,
       Type::Caption);
}
void section(const GfxRenderer& r, const Frame& f, Rect box, const char* label) {
  text(r, f, box, label, Type::Section);
}
void row(const GfxRenderer& r, const Frame& f, Rect b, const char* label, const char* value, bool focused,
         bool chevron) {
  r.drawLine(b.x, b.y + b.height - 1, b.x + b.width - 1, b.y + b.height - 1);
  if (focused && !f.touch) r.drawRoundedRect(b.x, b.y, b.width, b.height, 2, f.textDevice ? 0 : f.unit(9), true);
  const int pad = f.unit(10), reserved = chevron ? f.unit(22) : 0;
  const int valueWidth = value && *value ? std::min(b.width / 2, r.getTextWidth(font(Type::Value), value)) : 0;
  text(r, f, Rect{b.x + pad, b.y + 2, b.width - 2 * pad - reserved - valueWidth - (valueWidth ? pad : 0), b.height - 4},
       label, Type::Label);
  if (valueWidth)
    text(r, f, Rect{b.x + b.width - pad - reserved - valueWidth, b.y + 2, valueWidth, b.height - 4}, value,
         Type::Value);
  if (chevron) text(r, f, Rect{b.x + b.width - reserved, b.y + 2, reserved, b.height - 4}, "›", Type::Label);
}
void actions(const GfxRenderer& r, const Frame& f, const TouchActionButtons::Layout& layout, const char* const* labels,
             int primary, int focused) {
  // The retained helper owns shapes/focus and shared hit-test geometry; the
  // kit owns bounded typography, including long translated action labels.
  const char* empty[] = {"", "", "", ""};
  TouchActionButtons::draw(
      r, layout, empty, primary, focused, font(Type::Label),
      f.textDevice ? TouchActionButtons::Appearance::Text : TouchActionButtons::Appearance::Rounded, f.touch);
  for (uint8_t i = 0; i < layout.count; ++i) {
    const auto b = layout.buttons[i];
    char label[128];
    std::snprintf(label, sizeof(label), "%s%s", f.textDevice && i == focused ? "› " : "", labels[i]);
    text(r, f, Rect{b.x + f.unit(12), b.y + 2, b.width - f.unit(24), b.height - 4}, label, Type::Label, !f.textDevice,
         !(f.touch && i == primary));
  }
}
}  // namespace BookSyncPresentation

// Keep every kit component in real-image size measurements before its screens
// are migrated. Never invoked by production navigation.
extern "C" void booksync_presentation_link(const GfxRenderer& renderer) {
  namespace kit = BookSyncPresentation;
  const auto f = kit::frame(renderer, false, false);
  const auto c = f.content();
  kit::header(renderer, f, tr(STR_ACCOUNT_YOUR_SERVER), "+");
  kit::footer(renderer, f, tr(STR_ACCOUNT_YOUR_SERVER));
  kit::row(renderer, f, f.row(c.y), tr(STR_ACCOUNT_ACCOUNT), tr(STR_ACCOUNT_AVAILABLE), true, true);
  kit::section(renderer, f, f.row(c.y), tr(STR_UPGRADE_CARRIED));
  kit::paragraph(renderer, f, c, tr(STR_ACCOUNT_OUT_BODY), kit::Type::Title);
  const char* labels[] = {tr(STR_ACCOUNT_SIGN_IN), tr(STR_CANCEL)};
  kit::actions(renderer, f, f.actions(c.y, 2), labels, 0, 0);
  kit::actions(renderer, f, f.actions(c.y, 2, false), labels, 0, 0);
}
