#include <gtest/gtest.h>

#include "booksync/services/widgets/Continue.h"
#include "booksync/ui/widgets/Continue.h"
using namespace booksync;

TEST(ContinueProjection, KnownZeroIsDifferentFromMissing) {
  PersonalCard card;
  ContinueValues out;
  projectContinue(true, PersonalState::Ready, card, false, 0, out);
  EXPECT_FALSE(out.timeLeftKnown);
  card.timeLeft = {0, true};
  card.finishDay = {0, true};
  projectContinue(true, PersonalState::Ready, card, false, 0, out);
  EXPECT_TRUE(out.timeLeftKnown);
  EXPECT_EQ(out.timeLeftAmount, 0u);
  EXPECT_TRUE(out.finishDayKnown);
}

class ContinueStates : public testing::TestWithParam<PersonalState> {};
TEST_P(ContinueStates, ProjectionKeepsDistinctAvailability) {
  PersonalCard card;
  ContinueValues out;
  projectContinue(true, GetParam(), card, false, 0, out);
  EXPECT_TRUE(out.supplied);
  EXPECT_EQ(static_cast<unsigned>(out.state), static_cast<unsigned>(GetParam()));
}
INSTANTIATE_TEST_SUITE_P(IndependentStates, ContinueStates,
                         testing::Values(PersonalState::Ready, PersonalState::Empty, PersonalState::NotSaved,
                                         PersonalState::Off, PersonalState::Unsupported, PersonalState::Unavailable,
                                         PersonalState::Oversized));
TEST(ContinueProjection, NoBookProducesKnownEmpty) {
  PersonalCard card;
  ContinueValues out;
  projectContinue(false, PersonalState::Ready, card, false, 0, out);
  EXPECT_EQ(out.state, ContinueState::Empty);
}

TEST(ContinueProjection, StaleRetainsActualValues) {
  PersonalCard card;
  card.timeLeft = {7200, true};
  ContinueValues out;
  projectContinue(true, PersonalState::Ready, card, true, 2, out);
  EXPECT_TRUE(out.stale);
  EXPECT_EQ(out.ageDays, 2u);
  EXPECT_EQ(out.timeLeftAmount, 2u);
  EXPECT_EQ(out.timeLeftUnit, ContinueTimeUnit::Hours);
  EXPECT_TRUE(out.ageKnown);
}

TEST(ContinueRenderer, ApprovedMainSizeAndEveryState) {
  BookRow book;
  book.title[0] = 'A';
  book.readingKnown = true;
  book.page = 36;
  book.pages = 100;
  WidgetView view{WidgetState::Known, &book, 1, 1, false};
  for (unsigned state = 0; state <= unsigned(ContinueState::Oversize); ++state) {
    ContinueValues values;
    values.state = ContinueState(state);
    values.supplied = true;
    values.timeLeftAmount = 2;
    values.timeLeftUnit = ContinueTimeUnit::Hours;
    values.progressKnown = true;
    values.progressPercent = 36;
    values.timeLeftKnown = values.finishDayKnown = true;
    values.stale = true;
    widgets::Canvas canvas;
    widgets::Continue::paint(canvas, view, values, {456, 224, 20, false, true}, true);
    EXPECT_GT(canvas.count, 2u);
    bool resume = false;
    unsigned covers = 0;
    for (unsigned i = 0; i < canvas.count; ++i) {
      const auto& c = canvas.commands[i];
      EXPECT_GE(c.box.x, 0);
      EXPECT_GE(c.box.y, 0);
      EXPECT_GE(c.box.w, 0);
      EXPECT_GE(c.box.h, 0);
      EXPECT_LE(c.box.x + c.box.w, 456);
      EXPECT_LE(c.box.y + c.box.h, 224);
      if (c.kind == widgets::Drawing::Text && c.label == widgets::Label::Resume) resume = true;
      if (c.kind == widgets::Drawing::BookJacket) ++covers;
    }
    EXPECT_EQ(resume, values.state == ContinueState::Ready);
    EXPECT_EQ(covers, values.state == ContinueState::Ready ? 1u : 0u);
    if (values.state == ContinueState::Oversize || values.state == ContinueState::Error)
      EXPECT_EQ(canvas.commands[canvas.count - 1].label, widgets::Label::Retry);
  }
}

class ContinueSizes : public testing::TestWithParam<WidgetBox> {};
TEST_P(ContinueSizes, TextAndActionsRemainInsideSupportedOrRetainedSize) {
  const auto box = GetParam();
  BookRow book;
  book.title[0] = 'A';
  book.readingKnown = true;
  book.page = 36;
  book.pages = 100;
  ContinueValues values;
  values.supplied = true;
  values.state = ContinueState::Ready;
  values.stale = values.timeLeftKnown = values.finishDayKnown = values.progressKnown = true;
  values.progressPercent = 36;
  values.seriesLabel[0] = values.chapterLabel[0] = 'A';
  widgets::Canvas canvas;
  widgets::Continue::paint(canvas, {WidgetState::Known, &book, 1, 1, false}, values, box, true);
  ASSERT_GT(canvas.count, 0);
  int progressBottom = 0, resumeTop = box.height;
  for (unsigned i = 0; i < canvas.count; ++i) {
    const auto& command = canvas.commands[i];
    EXPECT_GE(command.box.x, 0);
    EXPECT_GE(command.box.y, 0);
    EXPECT_GE(command.box.w, 0);
    EXPECT_GE(command.box.h, 0);
    EXPECT_LE(command.box.x + command.box.w, box.width);
    EXPECT_LE(command.box.y + command.box.h, box.height);
    if (command.kind == widgets::Drawing::Fill && command.radius == 0 && command.box.h < 10)
      progressBottom = command.box.y + command.box.h;
    if (command.kind == widgets::Drawing::Border && command.radius == 24) {
      resumeTop = command.box.y;
      EXPECT_GE(command.box.w, 50);
      EXPECT_GE(command.box.h, 50);
    }
  }
  EXPECT_LE(progressBottom, resumeTop);
}
INSTANTIATE_TEST_SUITE_P(MainAndRetainedCustomSizes, ContinueSizes,
                         testing::Values(WidgetBox{456, 224, 25, false, true}, WidgetBox{456, 224, 26, false, false},
                                         WidgetBox{450, 223, 26, false, false, 2},
                                         WidgetBox{224, 224, 25, false, false}, WidgetBox{456, 188, 25, false, true}));

TEST(ContinueRenderer, NativeMainRetainsContextAndReadableFilledResume) {
  BookRow book;
  book.title[0] = 'A';
  ContinueValues values;
  values.supplied = true;
  values.state = ContinueState::Ready;
  values.seriesLabel[0] = values.chapterLabel[0] = 'A';
  widgets::Canvas canvas;
  widgets::Continue::paint(canvas, {WidgetState::Known, &book, 1, 1, false}, values, {456, 224, 26, false, true},
                           false);
  unsigned context = 0;
  bool filled = false, caption = false;
  for (unsigned i = 0; i < canvas.count; ++i) {
    const auto& c = canvas.commands[i];
    if (c.kind == widgets::Drawing::Title && !c.bold) ++context;
    if (c.kind == widgets::Drawing::Fill && c.radius == 24) {
      filled = !c.black && c.box.w == 108 && c.box.h == 50;
    }
    if (c.kind == widgets::Drawing::Text && c.label == widgets::Label::Resume) caption = c.black;
  }
  EXPECT_EQ(context, 2);
  EXPECT_TRUE(filled);
  EXPECT_TRUE(caption);
}

TEST(ContinueRenderer, OversizeHasItsApprovedDataReasonAndRetry) {
  ContinueValues values;
  values.supplied = true;
  values.state = ContinueState::Oversize;
  widgets::Canvas canvas;
  widgets::Continue::paint(canvas, {WidgetState::Oversize}, values, {450, 223, 26, false, false}, true);
  bool reason = false;
  for (unsigned i = 0; i < canvas.count; ++i)
    if (canvas.commands[i].kind == widgets::Drawing::Text && canvas.commands[i].label == widgets::Label::OversizedData)
      reason = true;
  EXPECT_TRUE(reason);
  EXPECT_EQ(canvas.commands[canvas.count - 1].label, widgets::Label::Retry);
}

TEST(ContinueProjection, ResolvedKnownZeroWinsOverNativePositionAndUnknownAgeStaysUnknown) {
  PersonalCard card;
  card.progress = {0, true};
  BookRow book;
  book.readingKnown = true;
  book.page = 36;
  book.pages = 100;
  ContinueValues out;
  projectContinue(true, PersonalState::Ready, card, true, 0, out, nullptr, false, &book);
  EXPECT_TRUE(out.progressKnown);
  EXPECT_EQ(out.progressPercent, 0);
  EXPECT_FALSE(out.ageKnown);
  card.progress = {};
  projectContinue(true, PersonalState::Ready, card, false, 0, out, nullptr, false, &book);
  EXPECT_TRUE(out.progressKnown);
  EXPECT_EQ(out.progressPercent, 36);
}
TEST(ContinueProjection, HostileCalendarAndUnrepresentableEstimateCannotFabricateValues) {
  PersonalCard card;
  card.finishDay = {UINT64_MAX, true};
  ContinueValues out;
  projectContinue(true, PersonalState::Ready, card, false, 0, out);
  EXPECT_EQ(out.state, ContinueState::Ready);
  EXPECT_FALSE(out.finishDayKnown);
  card.timeLeft = {UINT64_MAX, true};
  projectContinue(true, PersonalState::Ready, card, false, 0, out);
  EXPECT_EQ(out.state, ContinueState::Oversize);
  EXPECT_FALSE(out.timeLeftKnown);
  EXPECT_EQ(out.timeLeftAmount, 0u);
}

#include "../widget_geometry/TextGeometry.h"
TEST(ContinueGeometry, AllStatesAndContextStayInsideBothX4TilesWithoutTextOverlap) {
  constexpr WidgetState states[] = {WidgetState::Known,   WidgetState::Empty,       WidgetState::NotSaved,
                                    WidgetState::Off,     WidgetState::Unsupported, WidgetState::Error,
                                    WidgetState::Oversize};
  BookRow book{};
  book.title[0] = 'A';
  book.readingKnown = true;
  book.page = 36;
  book.pages = 100;
  for (auto profile : {WidgetBox{456, 224, 26, false, true, 2}, WidgetBox{450, 223, 26, false, false, 2}})
    for (auto state : states)
      for (bool stale : {false, true}) {
        widgets::Canvas commands;
        ContinueValues values{};
        values.supplied = true;
        values.stale = stale;
        values.ageKnown = true;
        values.ageDays = 2;
        values.progressKnown = values.timeLeftKnown = values.finishDayKnown = true;
        values.progressPercent = 36;
        values.timeLeftAmount = 2;
        values.timeLeftUnit = ContinueTimeUnit::Hours;
        values.finishWeekday = 3;
        values.seriesLabel[0] = 'S';
        values.chapterLabel[0] = 'C';
        values.state = state == WidgetState::Known         ? ContinueState::Ready
                       : state == WidgetState::Empty       ? ContinueState::Empty
                       : state == WidgetState::Off         ? ContinueState::Off
                       : state == WidgetState::Unsupported ? ContinueState::Unsupported
                       : state == WidgetState::Error       ? ContinueState::Error
                       : state == WidgetState::Oversize    ? ContinueState::Oversize
                                                           : ContinueState::NotSaved;
        widgets::Continue::paint(commands,
                                 {state, state == WidgetState::Known ? &book : nullptr, 1,
                                  static_cast<uint16_t>(state == WidgetState::Known ? 1 : 0), stale},
                                 values, profile, true);
        booksync::test::expectTextGeometry(commands, profile.width, profile.height);
      }
}

TEST(ContinueGeometry, ApprovedProgressAndClassicActionOutlineUseTheirExactCells) {
  BookRow book;
  book.title[0] = 'A';
  ContinueValues value;
  value.supplied = value.progressKnown = true;
  value.state = ContinueState::Ready;
  value.progressPercent = 36;
  for (bool touch : {false, true}) {
    widgets::Canvas canvas;
    widgets::Continue::paint(canvas, {WidgetState::Known, &book, 1, 1}, value, {456, 224, 25, false, touch}, true);
    bool bar = false, outline = false;
    for (uint8_t i = 0; i < canvas.count; ++i) {
      const auto& c = canvas.commands[i];
      if (c.kind == widgets::Drawing::Border && c.box.x == 144 && c.box.y == 125) {
        EXPECT_EQ(c.box.w, 248);
        EXPECT_EQ(c.box.h, 7);
        bar = true;
      }
      if (c.kind == widgets::Drawing::Border && c.radius == 27) {
        EXPECT_EQ(c.box.x, 140);
        EXPECT_EQ(c.box.y, 143);
        EXPECT_EQ(c.box.w, 116);
        EXPECT_EQ(c.box.h, 58);
        EXPECT_FALSE(c.black);
        EXPECT_EQ(c.thickness, 2);
        outline = true;
      }
    }
    EXPECT_TRUE(bar);
    EXPECT_EQ(outline, !touch);
  }
}
