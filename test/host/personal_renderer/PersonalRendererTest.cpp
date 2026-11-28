#include <gtest/gtest.h>

#include "booksync/ui/widgets/Discover.h"
#include "booksync/ui/widgets/Recommended.h"
using namespace booksync;
class PersonalStates : public testing::TestWithParam<WidgetState> {};
TEST_P(PersonalStates, EachCardNamesItsStateAndKeepsFailuresIndependent) {
  for (bool discovery : {false, true}) {
    widgets::Canvas commands;
    const WidgetView snapshot{GetParam()};
    if (discovery)
      widgets::Discover::paint(commands, snapshot, {456, 224, 26, false, true}, 0);
    else
      widgets::Recommended::paint(commands, snapshot, {224, 108, 20, false, true}, false);
    widgets::Label expected = widgets::Label::NotSaved;
    switch (GetParam()) {
      case WidgetState::Empty:
        expected = discovery ? widgets::Label::EmptyDiscover : widgets::Label::EmptyRecommended;
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
    bool message = false, retry = false;
    for (unsigned i = 0; i < commands.count; ++i) {
      const auto c = commands.commands[i];
      EXPECT_NE(c.kind, widgets::Drawing::BookJacket);
      if (c.kind == widgets::Drawing::Text && c.label == expected) message = true;
      if (c.kind == widgets::Drawing::Text && c.label == widgets::Label::Retry) retry = true;
      EXPECT_LE(c.box.x + c.box.w, discovery ? 456 : 224);
      EXPECT_LE(c.box.y + c.box.h, discovery ? 224 : 108);
    }
    EXPECT_TRUE(message);
    EXPECT_EQ(retry, GetParam() == WidgetState::Error || GetParam() == WidgetState::Oversize);
  }
}
INSTANTIATE_TEST_SUITE_P(SevenIndependentStates, PersonalStates,
                         testing::Values(WidgetState::Loading, WidgetState::NotSaved, WidgetState::Empty,
                                         WidgetState::Off, WidgetState::Unsupported, WidgetState::Error,
                                         WidgetState::Oversize));
TEST(PersonalRenderer, DiscoverBudgetsFourAndKeepsAdjacentFiftyPixelHeaderActions) {
  const auto request = widgets::Discover::request();
  EXPECT_EQ(request.limit, 4);
  EXPECT_EQ(request.source, WindowSource::Random);
  std::array<BookRow, 4> books{};
  widgets::Canvas commands;
  widgets::Discover::paint(commands, {WidgetState::Known, books.data(), 37, 4, true, 2, true},
                           {456, 224, 26, false, true}, 1);
  unsigned covers = 0, shuffle = 0;
  bool all = false, age = false;
  for (unsigned i = 0; i < commands.count; ++i) {
    const auto c = commands.commands[i];
    if (c.kind == widgets::Drawing::BookJacket) ++covers;
    if (c.kind == widgets::Drawing::ShuffleIcon) {
      ++shuffle;
      EXPECT_EQ(c.box.w, 50);
      EXPECT_EQ(c.box.h, 50);
      EXPECT_EQ(c.box.x, 315);
      EXPECT_EQ(c.box.x + c.box.w + 5, 370);
    }
    if (c.kind == widgets::Drawing::Text && c.label == widgets::Label::AllBooks) {
      all = true;
      EXPECT_EQ(c.first, 37);
    }
    if (c.kind == widgets::Drawing::Text && c.label == widgets::Label::SavedAge) {
      age = true;
      EXPECT_EQ(c.first, 2);
    }
  }
  EXPECT_EQ(covers, 4);
  EXPECT_EQ(shuffle, 1);
  EXPECT_TRUE(all);
  EXPECT_TRUE(age);
}
TEST(PersonalRenderer, RecommendedOneFortyTwoBySixtyFourCoverAndActualHistoryLabel) {
  EXPECT_EQ(widgets::Recommended::request().limit, 1);
  BookRow book{};
  book.title[0] = 'N';
  widgets::Canvas commands;
  widgets::Recommended::paint(commands, {WidgetState::Known, &book, 17, 1, true, 2, true}, {224, 108, 20, false, true},
                              true);
  unsigned covers = 0;
  bool history = false, age = false;
  for (unsigned i = 0; i < commands.count; ++i) {
    const auto c = commands.commands[i];
    EXPECT_GE(c.box.x, 0);
    EXPECT_GE(c.box.y, 0);
    EXPECT_LE(c.box.x + c.box.w, 224);
    EXPECT_LE(c.box.y + c.box.h, 108);
    if (c.kind == widgets::Drawing::BookJacket) {
      ++covers;
      EXPECT_EQ(c.box.w, 42);
      EXPECT_EQ(c.box.h, 64);
    }
    if (c.kind == widgets::Drawing::Text && c.label == widgets::Label::ReadingHistory) history = true;
    if (c.kind == widgets::Drawing::Text && c.label == widgets::Label::SavedAge) age = true;
  }
  EXPECT_EQ(covers, 1);
  EXPECT_TRUE(history);
  EXPECT_TRUE(age);
}

#include "../widget_geometry/TextGeometry.h"
TEST(PersonalGeometry, AllStatesAndSavedAgesFitBothX4TilesWithoutTextOverlap) {
  std::array<BookRow, 4> books{};
  for (bool discovery : {false, true})
    for (bool touch : {false, true})
      for (bool stale : {false, true})
        for (auto state : {WidgetState::Known, WidgetState::Empty, WidgetState::Loading, WidgetState::NotSaved,
                           WidgetState::Off, WidgetState::Unsupported, WidgetState::Error, WidgetState::Oversize}) {
          widgets::Canvas commands;
          WidgetBox box{discovery ? (touch ? 456 : 450) : (touch ? 224 : 221),
                        discovery ? (touch ? 224 : 223) : (touch ? 108 : 107), 20, false, touch};
          WidgetView snapshot{state, state == WidgetState::Known ? books.data() : nullptr,
                              37,    static_cast<uint16_t>(state == WidgetState::Known ? (discovery ? 4 : 1) : 0),
                              stale, 2,
                              true};
          if (discovery)
            widgets::Discover::paint(commands, snapshot, box, 0);
          else
            widgets::Recommended::paint(commands, snapshot, box, true);
          booksync::test::expectTextGeometry(commands, box.width, box.height);
        }
}
