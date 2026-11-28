#include <gtest/gtest.h>

#include "booksync/ui/widgets/Canvas.h"
#include "booksync/ui/widgets/Shelf.h"
using namespace booksync;
class BookListPresets : public testing::TestWithParam<WindowSource> {};
TEST_P(BookListPresets, MainUsesAuthoritativeHeaderAndBoundedPreview) {
  std::array<BookRow, 4> books{};
  for (auto& book : books) {
    book.title[0] = 'A';
    book.readingKnown = true;
    book.page = 36;
    book.pages = 100;
  }
  widgets::Canvas canvas;
  const auto source = GetParam();
  const uint16_t count = source == WindowSource::AlsoReading || source == WindowSource::OnDevice ? 3 : 4;
  widgets::Shelf::paint(canvas, {WidgetState::Known, books.data(), 10000, count, false}, source, count == 3 ? 1 : 0,
                        {456, 224, 26, false, true}, 0);
  unsigned covers = 0;
  bool all = false;
  for (unsigned i = 0; i < canvas.count; ++i) {
    const auto& c = canvas.commands[i];
    EXPECT_GE(c.box.x, 0);
    EXPECT_GE(c.box.y, 0);
    EXPECT_GE(c.box.w, 0);
    EXPECT_GE(c.box.h, 0);
    EXPECT_LE(c.box.x + c.box.w, 456);
    EXPECT_LE(c.box.y + c.box.h, 224);
    if (c.kind == widgets::Drawing::BookJacket) ++covers;
    if (c.kind == widgets::Drawing::Text && c.label == widgets::Label::AllBooks) {
      all = true;
      EXPECT_EQ(c.first, 10000);
      EXPECT_EQ(c.box.y, 12);
      EXPECT_GE(c.box.x, 300);
      EXPECT_GE(c.box.w, 50);
      EXPECT_EQ(c.box.h, 20);
    }
  }
  EXPECT_TRUE(all);
  EXPECT_EQ(covers, count);
}
INSTANTIATE_TEST_SUITE_P(NineSources, BookListPresets,
                         testing::Values(WindowSource::AlsoReading, WindowSource::Recent, WindowSource::WantToRead,
                                         WindowSource::AllFinished, WindowSource::PickBackUp, WindowSource::UpNext,
                                         WindowSource::FinishedYear, WindowSource::Unopened, WindowSource::OnDevice));
class BookListStates : public testing::TestWithParam<WidgetState> {};
TEST_P(BookListStates, IndependentStateDoesNotInventBooksOrCount) {
  widgets::Canvas canvas;
  widgets::Shelf::paint(canvas, {GetParam()}, WindowSource::Recent, 0, {456, 224, 26, false, false}, 0);
  ASSERT_GT(canvas.count, 0);
  widgets::Label expected = widgets::Label::NotSaved;
  switch (GetParam()) {
    case WidgetState::Empty:
      expected = widgets::Label::EmptyShelf;
      break;
    case WidgetState::Off:
      expected = widgets::Label::Off;
      break;
    case WidgetState::Unsupported:
      expected = widgets::Label::Unsupported;
      break;
    case WidgetState::Error:
      expected = widgets::Label::Error;
      break;
    case WidgetState::Oversize:
      expected = widgets::Label::OversizedData;
      break;
    default:
      break;
  }
  bool message = false;
  for (unsigned i = 0; i < canvas.count; ++i) {
    const auto& c = canvas.commands[i];
    EXPECT_NE(c.kind, widgets::Drawing::BookJacket);
    if (c.kind == widgets::Drawing::Text && c.label == expected) message = true;
    if (GetParam() != WidgetState::Empty) EXPECT_NE(c.label, widgets::Label::AllBooks);
  }
  EXPECT_TRUE(message);
}
INSTANTIATE_TEST_SUITE_P(IndependentStates, BookListStates,
                         testing::Values(WidgetState::Empty, WidgetState::NotSaved, WidgetState::Off,
                                         WidgetState::Unsupported, WidgetState::Error, WidgetState::Oversize));
TEST(BookListRenderer, StaleKeepsBooksAndHeaderCount) {
  std::array<BookRow, 4> rows{};
  widgets::Canvas canvas;
  widgets::Shelf::paint(canvas, {WidgetState::Known, rows.data(), 41, 4, true}, WindowSource::Recent, 0,
                        {456, 224, 26, false, true}, 1);
  unsigned covers = 0;
  bool saved = false;
  for (unsigned i = 0; i < canvas.count; ++i) {
    const auto& c = canvas.commands[i];
    if (c.kind == widgets::Drawing::BookJacket) ++covers;
    if (c.kind == widgets::Drawing::Text && c.label == widgets::Label::SavedUnknown) saved = true;
  }
  EXPECT_EQ(covers, 4);
  EXPECT_TRUE(saved);
}

#include "../widget_geometry/TextGeometry.h"
TEST(BookListGeometry, EveryPresetAndStateStaysInsideEachNativeProfileTileWithoutTextOverlap) {
  constexpr WindowSource sources[] = {WindowSource::AlsoReading,  WindowSource::Recent,     WindowSource::WantToRead,
                                      WindowSource::AllFinished,  WindowSource::PickBackUp, WindowSource::UpNext,
                                      WindowSource::FinishedYear, WindowSource::Unopened,   WindowSource::OnDevice};
  constexpr WidgetState states[] = {WidgetState::Known,   WidgetState::Empty,       WidgetState::NotSaved,
                                    WidgetState::Off,     WidgetState::Unsupported, WidgetState::Error,
                                    WidgetState::Oversize};
  std::array<BookRow, 4> books{};
  for (auto& book : books) {
    book.readingKnown = true;
    book.page = 36;
    book.pages = 100;
  }
  for (auto profile : {WidgetBox{456, 224, 26, false, true}, WidgetBox{450, 223, 26, false, false}})
    for (auto source : sources)
      for (auto state : states)
        for (bool stale : {false, true}) {
          widgets::Canvas commands;
          const uint16_t count = source == WindowSource::AlsoReading || source == WindowSource::OnDevice ? 3 : 4;
          widgets::Shelf::paint(commands,
                                {state, state == WidgetState::Known ? books.data() : nullptr, 37,
                                 static_cast<uint16_t>(state == WidgetState::Known ? count : 0), stale, 2, true},
                                source, count == 3 ? 1 : 0, profile, 0);
          booksync::test::expectTextGeometry(commands, profile.width, profile.height);
        }
}

TEST(BookListRenderer, OnCardBadgeRequiresIndependentPublishedFileEvidence) {
  using namespace booksync::widgets;
  BookRow books[4];
  FileAvailability files[] = {FileAvailability::OnCard, FileAvailability::Unknown, FileAvailability::ServerOnly,
                              FileAvailability::OnCard};
  WidgetView view{WidgetState::Known, books, 37, 4};
  view.files = files;
  Canvas commands;
  Shelf{}.paint(commands, view, WindowSource::Recent, 0, {456, 224, 26, false, true}, 255);
  unsigned badges = 0;
  for (uint8_t i = 0; i < commands.count; ++i)
    if (commands.commands[i].kind == Drawing::OnCardMark) ++badges;
  EXPECT_EQ(badges, 2u);
  commands.clear();
  view.files = nullptr;
  Shelf{}.paint(commands, view, WindowSource::Recent, 0, {456, 224, 26, false, true}, 255);
  for (uint8_t i = 0; i < commands.count; ++i) EXPECT_NE(commands.commands[i].kind, Drawing::OnCardMark);
}
