#include <gtest/gtest.h>

#include <cstring>

#include "booksync/ui/widgets/Library.h"
#include "booksync/ui/widgets/Lists.h"
using namespace booksync;
using namespace booksync::widgets;
namespace {
const PaintCommand* label(const Canvas& canvas, Label wanted) {
  for (uint8_t i = 0; i < canvas.count; ++i)
    if (canvas.commands[i].kind == Drawing::Text && canvas.commands[i].label == wanted) return &canvas.commands[i];
  return nullptr;
}
LibraryWidgetValues libraryData() {
  LibraryWidgetValues value;
  value.state = CollectionWidgetState::Ready;
  value.books = 2418;
  value.onCard = 312;
  value.booksKnown = value.onCardKnown = value.storageKnown = true;
  value.freeBytes = 4200000000;
  value.totalBytes = 32000000000;
  value.storageUsedPercent = 87;
  value.authorsKnown = value.seriesKnown = value.collectionsKnown = true;
  return value;
}
ListsWidgetValues listsData() {
  ListsWidgetValues value;
  value.state = CollectionWidgetState::Ready;
  value.countKnown = true;
  value.total = 9;
  value.count = 3;
  const char* names[] = {"Short reads", "Unread fiction", "Added this month"};
  const uint32_t books[] = {84, 132, 23};
  for (unsigned i = 0; i < 3; ++i) {
    std::strcpy(value.entries[i].name.data(), names[i]);
    value.entries[i].books = books[i];
    value.entries[i].booksKnown = true;
  }
  return value;
}
}  // namespace
TEST(CollectionRenderers, MainLibraryKeepsTwoSeparateCountsAndStorageWithoutInventedAll) {
  Canvas canvas;
  auto value = libraryData();
  LibraryCard renderer;
  renderer.paint(canvas, value, {224, 108, 20, false, true}, 0);
  ASSERT_NE(label(canvas, Label::LibraryBooksNumber), nullptr);
  EXPECT_EQ(label(canvas, Label::LibraryBooksNumber)->first, 2418);
  ASSERT_NE(label(canvas, Label::OnCardNumber), nullptr);
  EXPECT_EQ(label(canvas, Label::OnCardNumber)->first, 312);
  EXPECT_NE(label(canvas, Label::StorageFree), nullptr);
  EXPECT_EQ(label(canvas, Label::AllBooks), nullptr);
  EXPECT_EQ(label(canvas, Label::AuthorsLink), nullptr);
}
TEST(CollectionRenderers, OptionalLibraryAddsThreeSeparateFiftyPixelTargets) {
  Canvas canvas;
  auto value = libraryData();
  LibraryCard renderer;
  renderer.paint(canvas, value, {456, 224, 20, false, true}, 2);
  for (auto target : {uint8_t(1), uint8_t(2), uint8_t(3)}) {
    WidgetCell cell;
    ASSERT_TRUE(renderer.libraryTarget({456, 224, 20, false, true}, target, cell));
    EXPECT_GE(cell.width, 50);
    EXPECT_EQ(cell.height, 50);
    EXPECT_EQ(renderer.hitLibrary({456, 224, 20, false, true}, cell.x + cell.width / 2, cell.y + 25), target);
  }
  EXPECT_NE(label(canvas, Label::AuthorsLink), nullptr);
  EXPECT_NE(label(canvas, Label::SeriesLink), nullptr);
  EXPECT_NE(label(canvas, Label::CollectionsLink), nullptr);
}
TEST(CollectionRenderers, MainListsShowsOneRealListAndFullHeaderCount) {
  Canvas canvas;
  auto value = listsData();
  ListsCard renderer;
  renderer.paint(canvas, value, {224, 108, 20, false, true}, 0);
  ASSERT_NE(label(canvas, Label::AllLists), nullptr);
  EXPECT_EQ(label(canvas, Label::AllLists)->first, 9);
  EXPECT_LT(label(canvas, Label::AllLists)->box.y, 50);
  unsigned names = 0;
  for (uint8_t i = 0; i < canvas.count; ++i)
    if (canvas.commands[i].kind == Drawing::Title) {
      ++names;
      EXPECT_STREQ(canvas.commands[i].caption, "Short reads");
    }
  EXPECT_EQ(names, 1);
  ASSERT_NE(label(canvas, Label::ListBooksNumber), nullptr);
  EXPECT_EQ(label(canvas, Label::ListBooksNumber)->first, 84u);
  EXPECT_GT(label(canvas, Label::ListBooksNumber)->box.x, 150);
  EXPECT_NE(label(canvas, Label::ListBooksCaption), nullptr);
}
TEST(CollectionRenderers, OptionalListsShowsThreeSavedRowsWithIndependentTargets) {
  Canvas canvas;
  auto value = listsData();
  ListsCard renderer;
  renderer.paint(canvas, value, {456, 224, 20, false, true}, 2);
  unsigned names = 0;
  for (uint8_t i = 0; i < canvas.count; ++i)
    if (canvas.commands[i].kind == Drawing::Title) ++names;
  EXPECT_EQ(names, 3);
  WidgetCell previous{};
  for (uint8_t i = 1; i <= 3; ++i) {
    WidgetCell cell;
    ASSERT_TRUE(renderer.listTarget({456, 224, 20, false, true}, i, cell));
    EXPECT_GE(cell.height, 50);
    EXPECT_GE(cell.y, 50);
    if (i > 1) EXPECT_GE(cell.y, previous.y + previous.height);
    previous = cell;
    EXPECT_EQ(renderer.hitLists({456, 224, 20, false, true}, cell.x + 2, cell.y + 2), i);
  }
}
TEST(CollectionRenderers, MissingCountsRemainUnknownRatherThanBecomingZero) {
  Canvas canvas;
  auto value = libraryData();
  value.booksKnown = false;
  LibraryCard library;
  library.paint(canvas, value, {224, 108, 20, false, false}, 0);
  ASSERT_NE(label(canvas, Label::LibraryBooksNumber), nullptr);
  EXPECT_EQ(label(canvas, Label::LibraryBooksNumber)->second, 0);
  auto lists = listsData();
  lists.countKnown = false;
  ListsCard renderer;
  renderer.paint(canvas, lists, {224, 108, 20, false, false}, 0);
  ASSERT_NE(label(canvas, Label::AllLists), nullptr);
  EXPECT_EQ(label(canvas, Label::AllLists)->second, 0);
}
TEST(CollectionRenderers, KnownZeroAndEmptyAreDistinctFromMissing) {
  Canvas canvas;
  auto value = libraryData();
  value.books = value.onCard = 0;
  value.state = CollectionWidgetState::Empty;
  LibraryCard library;
  library.paint(canvas, value, {224, 108, 20, false, false}, 0);
  ASSERT_NE(label(canvas, Label::LibraryBooksNumber), nullptr);
  EXPECT_EQ(label(canvas, Label::LibraryBooksNumber)->first, 0);
  EXPECT_EQ(label(canvas, Label::LibraryBooksNumber)->second, 1);
  auto lists = listsData();
  lists.count = 0;
  lists.total = 0;
  lists.state = CollectionWidgetState::Empty;
  ListsCard renderer;
  renderer.paint(canvas, lists, {224, 108, 20, false, false}, 0);
  EXPECT_NE(label(canvas, Label::EmptyLists), nullptr);
  ASSERT_NE(label(canvas, Label::AllLists), nullptr);
  EXPECT_EQ(label(canvas, Label::AllLists)->first, 0);
}
TEST(CollectionRenderers, StaleKeepsActualDataAndOnlyKnownAge) {
  Canvas canvas;
  auto value = listsData();
  value.stale = true;
  value.ageDays = 2;
  ListsCard lists;
  lists.paint(canvas, value, {224, 108, 20, false, false}, 0);
  EXPECT_NE(label(canvas, Label::SavedUnknown), nullptr);
  EXPECT_EQ(label(canvas, Label::SavedAge), nullptr);
  value.ageKnown = true;
  lists.paint(canvas, value, {456, 224, 20, false, false}, 0);
  ASSERT_NE(label(canvas, Label::SavedAge), nullptr);
  EXPECT_EQ(label(canvas, Label::SavedAge)->first, 2);
  auto summary = libraryData();
  summary.stale = summary.ageKnown = true;
  summary.ageDays = 2;
  LibraryCard library;
  library.paint(canvas, summary, {224, 108, 20, false, false}, 0);
  EXPECT_NE(label(canvas, Label::SavedAge), nullptr);
  EXPECT_NE(label(canvas, Label::LibraryBooksNumber), nullptr);
}
class CollectionStates : public testing::TestWithParam<std::pair<CollectionWidgetState, Label>> {};
TEST_P(CollectionStates, LibraryMainAndOptionalRetainEachState) {
  LibraryCard renderer;
  Canvas canvas;
  auto value = libraryData();
  value.state = GetParam().first;
  for (auto box : {WidgetBox{224, 108, 20, false, true}, WidgetBox{456, 224, 20, false, true}}) {
    renderer.paint(canvas, value, box, 0);
    EXPECT_NE(label(canvas, GetParam().second), nullptr);
    EXPECT_EQ(label(canvas, Label::LibraryBooksNumber), nullptr);
  }
}
TEST_P(CollectionStates, ListsMainAndOptionalRetainEachState) {
  ListsCard renderer;
  Canvas canvas;
  auto value = listsData();
  value.state = GetParam().first;
  for (auto box : {WidgetBox{224, 108, 20, false, true}, WidgetBox{456, 224, 20, false, true}}) {
    renderer.paint(canvas, value, box, 0);
    EXPECT_NE(label(canvas, GetParam().second), nullptr);
    EXPECT_EQ(label(canvas, Label::ListBooks), nullptr);
  }
}
INSTANTIATE_TEST_SUITE_P(IndependentStates, CollectionStates,
                         testing::Values(std::pair{CollectionWidgetState::NotSaved, Label::NotSaved},
                                         std::pair{CollectionWidgetState::Off, Label::Off},
                                         std::pair{CollectionWidgetState::Unsupported, Label::Unsupported},
                                         std::pair{CollectionWidgetState::Error, Label::Error},
                                         std::pair{CollectionWidgetState::Oversize, Label::OversizedData}));
TEST(CollectionRenderers, OversizeKeepsRetryAndNoOldListNames) {
  Canvas canvas;
  auto value = listsData();
  value.state = CollectionWidgetState::Oversize;
  ListsCard renderer;
  renderer.paint(canvas, value, {456, 224, 20, false, true}, 0);
  EXPECT_NE(label(canvas, Label::Retry), nullptr);
  for (uint8_t i = 0; i < canvas.count; ++i) EXPECT_NE(canvas.commands[i].kind, Drawing::Title);
}
TEST(CollectionRenderers, OutOfCardAndUnapprovedNarrowTargetsCannotDispatch) {
  ListsCard lists;
  LibraryCard library;
  WidgetCell cell;
  EXPECT_EQ(lists.hitLists({224, 108, 20, false, false}, 225, 25), 255);
  EXPECT_EQ(library.hitLibrary({224, 108, 20, false, false}, -1, 25), 255);
  EXPECT_FALSE(lists.listTarget({224, 108, 20, false, false}, 2, cell));
  EXPECT_FALSE(library.libraryTarget({224, 108, 20, false, false}, 1, cell));
}

TEST(CollectionRenderers, HostRejectionOverridesOtherwiseReadyValues) {
  Canvas canvas;
  LibraryCard library;
  auto summary = libraryData();
  library.paint(canvas, summary, {224, 108, 20, false, true}, 0, WidgetState::Oversize);
  EXPECT_NE(label(canvas, Label::OversizedData), nullptr);
  EXPECT_EQ(label(canvas, Label::LibraryBooksNumber), nullptr);
  ListsCard lists;
  auto data = listsData();
  lists.paint(canvas, data, {456, 224, 20, false, true}, 0, WidgetState::Error);
  EXPECT_NE(label(canvas, Label::Error), nullptr);
  EXPECT_EQ(label(canvas, Label::ListBooks), nullptr);
}

#include "../widget_geometry/TextGeometry.h"
TEST(CollectionGeometry, BothSizesAndAllStatesFitEachX4TileWithoutTextOverlap) {
  for (bool expanded : {false, true})
    for (bool touch : {false, true})
      for (bool stale : {false, true})
        for (auto state : {CollectionWidgetState::Ready, CollectionWidgetState::Empty, CollectionWidgetState::NotSaved,
                           CollectionWidgetState::Off, CollectionWidgetState::Unsupported, CollectionWidgetState::Error,
                           CollectionWidgetState::Oversize}) {
          WidgetBox box{expanded ? (touch ? 456 : 450) : (touch ? 224 : 221),
                        expanded ? (touch ? 224 : 223) : (touch ? 108 : 107), 26, false, touch};
          LibraryWidgetValues summary{};
          summary.state = state;
          summary.stale = stale;
          summary.ageKnown = true;
          summary.ageDays = 2;
          ListsWidgetValues lists{};
          lists.state = state;
          lists.stale = stale;
          lists.ageKnown = true;
          lists.ageDays = 2;
          lists.count = state == CollectionWidgetState::Ready ? (expanded ? 3 : 1) : 0;
          for (uint8_t i = 0; i < lists.count; ++i) lists.entries[i].name[0] = 'A';
          widgets::Canvas commands;
          widgets::LibraryCard library;
          widgets::ListsCard namedLists;
          library.paint(commands, summary, box, 0,
                        state == CollectionWidgetState::Ready         ? WidgetState::Known
                        : state == CollectionWidgetState::Empty       ? WidgetState::Empty
                        : state == CollectionWidgetState::Off         ? WidgetState::Off
                        : state == CollectionWidgetState::Unsupported ? WidgetState::Unsupported
                        : state == CollectionWidgetState::Error       ? WidgetState::Error
                        : state == CollectionWidgetState::Oversize    ? WidgetState::Oversize
                                                                      : WidgetState::NotSaved);
          booksync::test::expectTextGeometry(commands, box.width, box.height);
          commands.clear();
          namedLists.paint(commands, lists, box, 0,
                           state == CollectionWidgetState::Ready         ? WidgetState::Known
                           : state == CollectionWidgetState::Empty       ? WidgetState::Empty
                           : state == CollectionWidgetState::Off         ? WidgetState::Off
                           : state == CollectionWidgetState::Unsupported ? WidgetState::Unsupported
                           : state == CollectionWidgetState::Error       ? WidgetState::Error
                           : state == CollectionWidgetState::Oversize    ? WidgetState::Oversize
                                                                         : WidgetState::NotSaved);
          booksync::test::expectTextGeometry(commands, box.width, box.height);
        }
}

// Independent coordinates from the approved compact mockup/CSS, including its
// 10-pixel column padding, 12-pixel button gaps and 50-pixel list-link rows.
TEST(CollectionGeometry, ApprovedColumnsButtonsAndListRowsStayAtTheirCropCoordinates) {
  Canvas canvas;
  LibraryCard{}.paint(canvas, libraryData(), {224, 108, 26, false, true}, 255);
  ASSERT_NE(label(canvas, Label::LibraryBooksNumber), nullptr);
  EXPECT_EQ(label(canvas, Label::LibraryBooksNumber)->box.y, 31);
  EXPECT_EQ(label(canvas, Label::OnCardNumber)->box.x, 128);
  EXPECT_EQ(label(canvas, Label::OnCardCaption)->box.x, 128);
  for (uint8_t i = 1; i <= 3; ++i) {
    WidgetCell cell;
    ASSERT_TRUE(LibraryCard{}.libraryTarget({456, 224, 26, false, true}, i, cell));
    EXPECT_EQ(cell.x, i == 1 ? 12 : i == 2 ? 160 : 308);
    EXPECT_EQ(cell.y, 160);
    EXPECT_EQ(cell.width, 136);
    EXPECT_EQ(cell.height, 50);
    ASSERT_TRUE(ListsCard{}.listTarget({456, 224, 26, false, true}, i, cell));
    EXPECT_EQ(cell.y, i == 1 ? 51 : i == 2 ? 101 : 151);
    EXPECT_EQ(cell.height, 50);
  }
}
