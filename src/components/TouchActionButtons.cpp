#include "TouchActionButtons.h"

#include <GfxRenderer.h>

#include <algorithm>

#include "fontIds.h"

namespace TouchActionButtons {

Layout vertical(const Rect container, const uint8_t count, const int buttonHeight, const int gap) {
  Layout result;
  result.container = container;
  result.count = std::min<uint8_t>(count, static_cast<uint8_t>(kMaxButtons));
  if (result.count == 0 || container.width <= 0 || container.height <= 0) return result;

  const int safeHeight = std::max(1, buttonHeight);
  const int safeGap = std::max(0, gap);
  const int totalHeight = safeHeight * result.count + safeGap * (result.count - 1);
  const int firstY = container.y + std::max(0, container.height - totalHeight);
  for (uint8_t i = 0; i < result.count; ++i) {
    result.buttons[i] = Rect{container.x, firstY + i * (safeHeight + safeGap), container.width, safeHeight};
  }
  return result;
}

Layout horizontal(const Rect container, const uint8_t count, const int gap) {
  Layout result;
  result.container = container;
  result.count = std::min<uint8_t>(count, kMaxButtons);
  if (!result.count || container.width <= 0 || container.height <= 0) return result;
  const int safeGap = std::max(0, gap);
  const int width = (container.width - safeGap * (result.count - 1)) / result.count;
  if (width <= 0) {
    result.count = 0;
    return result;
  }
  for (uint8_t i = 0; i < result.count; ++i)
    result.buttons[i] = Rect{container.x + i * (width + safeGap), container.y, width, container.height};
  return result;
}

int indexAt(const Layout& layout, const int x, const int y, const int touchPadding) {
  const int padding = std::max(0, touchPadding);
  for (uint8_t i = 0; i < layout.count; ++i) {
    const Rect& button = layout.buttons[i];
    if (button.width <= 0 || button.height <= 0) continue;
    if (x >= button.x - padding && x < button.x + button.width + padding && y >= button.y - padding &&
        y < button.y + button.height + padding) {
      return i;
    }
  }
  return -1;
}

void draw(const GfxRenderer& renderer, const Layout& layout, const char* const* labels, const int primaryIndex,
          const int selectedIndex, int fontId, const Appearance appearance, const bool touch) {
  if (labels == nullptr) return;
  if (fontId == 0) fontId = UI_10_FONT_ID;

  for (uint8_t i = 0; i < layout.count; ++i) {
    const Rect& button = layout.buttons[i];
    const bool selected = static_cast<int>(i) == selectedIndex;
    const bool branded = appearance != Appearance::Standard;
    if (!branded) {
      // Preserve legacy callers, including callers with an active text clip.
      if (selected) renderer.fillRect(button.x, button.y, button.width, button.height, true);
      renderer.drawRect(button.x, button.y, button.width, button.height, true);
      const char* label = labels[i] ? labels[i] : "";
      const auto style = static_cast<int>(i) == primaryIndex ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
      const int width = renderer.getTextWidth(fontId, label, style);
      renderer.drawText(fontId, button.x + (button.width - width) / 2,
                        button.y + (button.height - renderer.getLineHeight(fontId)) / 2, label, !selected, style);
      continue;
    }
    const bool filled = touch && static_cast<int>(i) == primaryIndex;
    const int radius = appearance == Appearance::Rounded ? button.height / 2 : 0;
    if (filled) renderer.fillRoundedRect(button.x, button.y, button.width, button.height, radius, Color::Black);
    renderer.drawRoundedRect(button.x, button.y, button.width, button.height, 1, radius, true);
    if (branded && !touch && selected)
      renderer.drawRoundedRect(button.x - 4, button.y - 4, button.width + 8, button.height + 8, 2, radius, true);
    const char* label = labels[i] != nullptr ? labels[i] : "";
    const EpdFontFamily::Style style = EpdFontFamily::BOLD;
    const int textWidth = renderer.getTextWidth(fontId, label, style);
    const int textY = button.y + (button.height - renderer.getLineHeight(fontId)) / 2;
    const int textX = appearance == Appearance::Text ? button.x + 12 : button.x + (button.width - textWidth) / 2;
    renderer.beginTextClip(button.x + 6, button.y + 2, button.width - 12, button.height - 4);
    renderer.drawText(fontId, textX, textY, label, !filled, style);
    renderer.endTextClip();
  }
}

}  // namespace TouchActionButtons
