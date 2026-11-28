#pragma once
#include <gtest/gtest.h>

#include "booksync/ui/widgets/Canvas.h"
namespace booksync::test {
inline bool textCommand(widgets::PaintCommand command) {
  return command.kind == widgets::Drawing::Text || command.kind == widgets::Drawing::Title ||
         command.kind == widgets::Drawing::SerifTitle;
}
inline void expectTextGeometry(const widgets::Canvas& commands, int width, int height) {
  for (uint8_t i = 0; i < commands.count; ++i) {
    const auto first = commands.commands[i];
    if (!textCommand(first)) continue;
    SCOPED_TRACE(testing::Message() << "text command " << unsigned(i) << " label " << unsigned(first.label));
    EXPECT_GE(first.box.x, 0);
    EXPECT_GE(first.box.y, 0);
    EXPECT_GT(first.box.w, 0);
    EXPECT_GT(first.box.h, 0);
    EXPECT_LE(first.box.x + first.box.w, width);
    EXPECT_LE(first.box.y + first.box.h, height);
    for (uint8_t j = i + 1; j < commands.count; ++j) {
      const auto second = commands.commands[j];
      if (!textCommand(second)) continue;
      const bool overlaps = first.box.x < second.box.x + second.box.w && second.box.x < first.box.x + first.box.w &&
                            first.box.y < second.box.y + second.box.h && second.box.y < first.box.y + first.box.h;
      EXPECT_FALSE(overlaps) << "overlaps text command " << unsigned(j) << " label " << unsigned(second.label);
    }
  }
}
}  // namespace booksync::test
