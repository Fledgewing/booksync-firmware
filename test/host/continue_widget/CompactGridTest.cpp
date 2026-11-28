#include <gtest/gtest.h>

#include "booksync/ui/CompactGrid.h"
using namespace booksync;
TEST(CompactGrid, BothX4ProfilesUseExactApprovedMainAndOptionalSizes) {
  for (bool classic : {false, true}) {
    (void)classic;
    const auto grid = CompactGrid::forHomePanel(480, 800, 3, 3, 3, 3);
    const auto main = grid.compactTile({WidgetKind::Browse, WindowSource::OnDevice, 0, 0, 1, 1});
    EXPECT_EQ(main.x, 12);
    EXPECT_EQ(main.y, 70);
    EXPECT_EQ(main.width, 224);
    EXPECT_EQ(main.height, 108);
    const auto optional = grid.compactTile({WidgetKind::Continue, WindowSource::Continue, 0, 0, 2, 2});
    EXPECT_EQ(optional.width, 456);
    EXPECT_EQ(optional.height, 224);
    const auto last = grid.compactTile({WidgetKind::Continue, WindowSource::Continue, 0, 4, 2, 2});
    EXPECT_EQ(last.y + last.height, 758);
  }
}
TEST(CompactGrid, LargerCalibratedInsetsRemainSafeAndInvalidGridFailsClosed) {
  auto grid = CompactGrid::forHomePanel(480, 800, 80, 24, 60, 20);
  auto tile = grid.compactTile({WidgetKind::Continue, WindowSource::Continue, 0, 0, 2, 2});
  EXPECT_EQ(tile.x, 20);
  EXPECT_EQ(tile.y, 80);
  EXPECT_LE(tile.x + tile.width, 456);
  EXPECT_LE(tile.y + tile.height, 740);
  grid.rows = 0;
  EXPECT_EQ(grid.compactTile({}).height, 0);
}
TEST(CompactGrid, StoredSevenRowGridKeepsItsRowCount) {
  const auto grid = CompactGrid::forHomePanel(480, 800, 3, 3, 3, 3, 2, 7);
  const auto tile = grid.compactTile({WidgetKind::Continue, WindowSource::Continue, 0, 0, 2, 2});
  EXPECT_GT(tile.height, 0);
  EXPECT_LT(tile.height, 210);
}
