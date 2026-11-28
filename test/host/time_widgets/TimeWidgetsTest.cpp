#include <gtest/gtest.h>

#include <string>

#include "booksync/services/LayoutEditor.h"
#include "booksync/services/LayoutGeometry.h"
#include "booksync/services/widgets/Clock.h"
#include "booksync/services/widgets/Moon.h"
#include "booksync/ui/widgets/Clock.h"
#include "booksync/ui/widgets/Moon.h"
#include "booksync/ui/widgets/Shelf.h"
using namespace booksync;
namespace {
ClockStatus known() { return {ClockEvidence::Synchronized, 1795789920, 0, {2026, 11, 27, 5, 14, 32, 0}}; }
class TimeStates : public testing::TestWithParam<TimeCardState> {};
constexpr TimeCardState states[] = {TimeCardState::NotSaved, TimeCardState::Ready,       TimeCardState::Empty,
                                    TimeCardState::Off,      TimeCardState::Unsupported, TimeCardState::Unavailable,
                                    TimeCardState::Oversize};
void bounds(const widgets::TimeDrawing& drawing, WidgetBox box) {
  for (unsigned i = 0; i < drawing.count; ++i) {
    const auto& mark = drawing.marks[i];
    const auto& c = mark.cell;
    EXPECT_GE(c.x, 0);
    EXPECT_GE(c.y, 0);
    EXPECT_GT(c.width, 0);
    EXPECT_GT(c.height, 0);
    EXPECT_LE(c.x + c.width, box.width);
    EXPECT_LE(c.y + c.height, box.height);
    if (mark.line == widgets::TimeLine::Retry) {
      EXPECT_GE(c.width, 50);
      EXPECT_GE(c.height, 50);
    }
  }
}
TEST_P(TimeStates, ClockProjectionAndBothProfileDrawingsAreIndependent) {
  ClockWidget out;
  projectClockWidget(known(), {GetParam()}, out);
  EXPECT_EQ(out.status.state, GetParam());
  EXPECT_EQ(out.reading.utcSeconds, GetParam() == TimeCardState::Ready ? 1795789920 : 0);
  for (bool touch : {false, true}) {
    WidgetBox box{224, 108, 16, false, touch};
    widgets::TimeDrawing drawing;
    ASSERT_TRUE(widgets::Clock::layoutTime(out, box, drawing));
    bounds(drawing, box);
    EXPECT_EQ(drawing.marks[0].line, widgets::TimeLine::Title);
  }
}
TEST_P(TimeStates, MoonProjectionAndBothProfileDrawingsAreIndependent) {
  MoonWidget out;
  projectMoonWidget(known(), {GetParam()}, out);
  EXPECT_EQ(out.status.state, GetParam());
  for (bool touch : {false, true}) {
    WidgetBox box{224, 108, 16, false, touch};
    widgets::TimeDrawing drawing;
    ASSERT_TRUE(widgets::Moon::layoutTime(out, box, drawing));
    bounds(drawing, box);
    if (GetParam() == TimeCardState::Ready)
      EXPECT_GT(drawing.moon.width, 0);
    else
      EXPECT_EQ(drawing.moon.width, 0);
  }
}
INSTANTIATE_TEST_SUITE_P(EveryState, TimeStates, testing::ValuesIn(states));
TEST(TimeProjection, UnknownAndDateLessKnownSourcesAreNotZeroClocks) {
  for (auto input : {ClockStatus{}, ClockStatus{ClockEvidence::LiveRtc}}) {
    ClockWidget clock;
    MoonWidget moon;
    projectClockWidget(input, {}, clock);
    projectMoonWidget(input, {}, moon);
    EXPECT_EQ(clock.status.state, TimeCardState::NotSaved);
    EXPECT_FALSE(clock.reading.hasDateTime());
    EXPECT_EQ(moon.status.state, TimeCardState::NotSaved);
  }
}
TEST(TimeProjection, NativeDisplayPreferencePreservesCivilHour) {
  ClockWidget clock;
  projectClockWidget(known(), {TimeCardState::Ready, 0, true, false, true}, clock);
  EXPECT_TRUE(clock.twelveHour);
  EXPECT_EQ(clock.reading.local.hour, 14);
  EXPECT_EQ(timeWidgetBit(static_cast<TimeWidget>(255)), 0);
}
TEST(TimeProjection, KnownMidnightRemainsReady) {
  ClockWidget clock;
  projectClockWidget({ClockEvidence::Retained, 1835395200, 0, {2028, 2, 29}}, {}, clock);
  EXPECT_EQ(clock.status.state, TimeCardState::Ready);
  EXPECT_EQ(clock.reading.local.hour, 0);
}
TEST(TimeProjection, StaleRetainsEachFaceAndAgeWithoutChangingSource) {
  const TimeProjection options{TimeCardState::Ready, 2, true, true};
  ClockWidget clock;
  MoonWidget moon;
  projectClockWidget(known(), options, clock);
  projectMoonWidget(known(), options, moon);
  EXPECT_TRUE(clock.status.stale);
  EXPECT_EQ(clock.status.ageDays, 2u);
  EXPECT_EQ(clock.reading.utcSeconds, 1795789920);
  EXPECT_TRUE(moon.status.stale);
  EXPECT_EQ(moon.status.ageDays, 2u);
  EXPECT_EQ(moon.status.state, TimeCardState::Ready);
}
TEST(TimeProjection, DisabledSourceDoesNotTurnHistoryIntoEmpty) {
  ClockWidget clock;
  MoonWidget moon;
  TimeProjection options;
  options.enabled = false;
  projectClockWidget(known(), options, clock);
  projectMoonWidget(known(), options, moon);
  EXPECT_EQ(clock.status.state, TimeCardState::Off);
  EXPECT_EQ(moon.status.state, TimeCardState::Off);
}
TEST(TimeProjection, MeanEstimateMatchesIndependentNearNewAndFullFixtures) {
  // Independent calendar dates near the November 2026 primary phases. The
  // approximate mean-lunation view intentionally has no exact-ephemeris claim.
  MoonWidget fresh, full;
  projectMoonWidget({ClockEvidence::LiveRtc, 1794225600, 0, {2026, 11, 9, 0, 12}}, {}, fresh);
  projectMoonWidget({ClockEvidence::LiveRtc, 1795521600, 0, {2026, 11, 24, 0, 12}}, {}, full);
  EXPECT_EQ(fresh.phase, 0);
  EXPECT_LE(fresh.illumination, 3);
  EXPECT_EQ(full.phase, 4);
  EXPECT_GE(full.illumination, 97);
  EXPECT_LE(full.nextSeconds, 638000u);
  EXPECT_LT(full.nextPhase, 8);
}
TEST(TimeDrawing, OversizeIsRefusedBeforeFaceAndX3DrawsNothing) {
  ClockWidget clock;
  MoonWidget moon;
  projectClockWidget(known(), {}, clock);
  projectMoonWidget(known(), {}, moon);
  ++clock.status.bytes;
  ++moon.status.bytes;
  EXPECT_EQ(widgets::boundedTimeCard(clock.status, sizeof(clock)), TimeCardState::Oversize);
  EXPECT_EQ(widgets::boundedTimeCard(moon.status, sizeof(moon)), TimeCardState::Oversize);
  widgets::TimeDrawing drawing;
  WidgetBox box{224, 108, 16, true, false};
  EXPECT_FALSE(widgets::Clock::layoutTime(clock, box, drawing));
  EXPECT_EQ(drawing.count, 0);
  EXPECT_FALSE(widgets::Moon::layoutTime(moon, box, drawing));
  EXPECT_EQ(drawing.count, 0);
}
class Fixture : public Library {
 public:
  HomePage page{};
  LibraryWindow window_{};
  TimeWidgetBatch time{};
  WindowBatchRequest lastBatch{};
  unsigned batches = 0;
  mutable unsigned reads = 0, timeReads = 0;
  unsigned commands = 0;
  Capabilities capabilities() const override { return {}; }
  Snapshot snapshot() const override { return {}; }
  HomePage homePage() const override { return page; }
  bool requestHomePage(uint8_t) override { return true; }
  Subscription subscribe() const override { return {window_.publication, 1, true}; }
  bool requestBatch(WindowBatchRequest batch) override {
    lastBatch = batch;
    ++batches;
    time.requested = batch.timeWidgets;
    ++window_.publication;
    return true;
  }
  void readWindow(LibraryWindow& out) const override {
    ++reads;
    out = window_;
  }
  void readTimeWidgets(TimeWidgetBatch& out) const override {
    ++timeReads;
    out = time;
  }
  CommandStatus submit(Intent) override {
    ++commands;
    return CommandStatus::Accepted;
  }
  Fixture() {
    page.revision = 1;
    page.count = 2;
    page.rows = 6;
    page.widgets[0] = {WidgetKind::Clock};
    page.widgets[1] = {WidgetKind::Moon};
    page.widgets[0].w = page.widgets[0].h = page.widgets[1].w = page.widgets[1].h = 1;
    projectClockWidget(known(), {}, time.clock);
    projectMoonWidget(known(), {}, time.moon);
  }
};
TEST(TimeHost, OneBatchOneCopyAndNoWorkFromReadonlyViews) {
  Fixture f;
  WidgetHost host(f);
  host.enter();
  ASSERT_TRUE(host.pollWidgets());
  EXPECT_EQ(f.batches, 1u);
  EXPECT_EQ(f.reads, 1u);
  EXPECT_EQ(f.timeReads, 1u);
  for (int repeat = 0; repeat < 3; ++repeat) {
    EXPECT_EQ(host.view(0).viewState, WidgetState::Known);
    EXPECT_EQ(host.view(1).viewState, WidgetState::Known);
    EXPECT_EQ(host.dispatchWidget(0), CommandStatus::Unavailable);
    EXPECT_EQ(host.dispatchWidget(1), CommandStatus::Unavailable);
    EXPECT_EQ(host.widgetRows(0, {224, 108}), 0);
    EXPECT_EQ(host.hitWidgetRow(1, {224, 108}, 50, 50), 255);
  }
  EXPECT_EQ(f.commands, 0u);
  EXPECT_EQ(f.batches, 1u);
  EXPECT_EQ(f.timeReads, 1u);
}
TEST(TimeHost, OversizeAndRetryLeaveSiblingFaceUsable) {
  Fixture f;
  ++f.time.clock.status.bytes;
  WidgetHost host(f);
  host.enter();
  host.pollWidgets();
  EXPECT_EQ(host.view(0).viewState, WidgetState::Oversize);
  EXPECT_EQ(host.view(1).viewState, WidgetState::Known);
  EXPECT_TRUE(host.retryWidget(0));
  EXPECT_EQ(f.batches, 2u);
  EXPECT_FALSE(host.retryWidget(1));
}
TEST(TimeHost, X3NeverRequestsOrPublishesNewWidgets) {
  Fixture f;
  f.page.textOnly = true;
  WidgetHost host(f);
  host.enter();
  host.pollWidgets();
  EXPECT_EQ(f.time.requested, 0);
  EXPECT_EQ(f.timeReads, 0u);
  EXPECT_EQ(host.clockCard(0), nullptr);
}
TEST(TimeHost, ScalarCardsDoNotShrinkAnAvailableFourCoverBudget) {
  Fixture f;
  f.page.count = 9;
  f.page.widgets[0] = {WidgetKind::Shelf, WindowSource::Recent, 0, 0, 2, 2, 1};
  for (unsigned i = 1; i < 9; ++i)
    f.page.widgets[i] = {
        WidgetKind::Clock, WindowSource::Unknown, uint8_t((i - 1) % 2), uint8_t(2 + (i - 1) / 2), 1, 1};
  WidgetHost host(f);
  host.enter();
  host.pollWidgets();
  EXPECT_EQ(f.lastBatch.windows[0].limit, widgets::Shelf::request(WindowSource::Recent).limit);
  EXPECT_EQ(f.batches, 1u);
}
TEST(TimeHost, InvalidTypedValuesAreIsolatedBeforePainting) {
  Fixture f;
  f.time.moon.phase = 255;
  WidgetHost host(f);
  host.enter();
  host.pollWidgets();
  EXPECT_EQ(host.view(0).viewState, WidgetState::Known);
  EXPECT_EQ(host.view(1).viewState, WidgetState::Error);
  EXPECT_EQ(host.hitWidgetRow(1, {224, 108}, 180, 80), 0);
  EXPECT_EQ(host.hitWidgetRow(1, {224, 108}, 20, 40), 255);
}
TEST(TimeLayout, NewClockAndMoonUseMainSizeAndStayHiddenOnX3) {
  for (auto kind : {WidgetKind::Clock, WidgetKind::Moon}) {
    LayoutInput input = Layout::defaults();
    std::string raw(input.bytes, '\0');
    ASSERT_TRUE(input.read(input.context, 0, raw.data(), raw.size()));
    LayoutEditor editor;
    LayoutEdit edit;
    edit.kind = LayoutEditKind::Add;
    edit.widget = kind;
    edit.revision = 1;
    ASSERT_EQ(editor.prepare(LayoutInput::memory(raw.data(), raw.size()), "x4", edit), LayoutEditStatus::Saved);
    const auto output = editor.output();
    std::string saved(output.bytes, '\0');
    ASSERT_TRUE(output.read(output.context, 0, saved.data(), saved.size()));
    Layout parser;
    HomePage x4, x3;
    ASSERT_TRUE(parser.project(LayoutInput::memory(saved.data(), saved.size()), "x4", 0, x4));
    ASSERT_TRUE(parser.project(LayoutInput::memory(saved.data(), saved.size()), "x3", 0, x3));
    ASSERT_EQ(x4.count, 4);
    EXPECT_EQ(x4.widgets[3].kind, kind);
    EXPECT_EQ(x4.widgets[3].w, 1);
    EXPECT_EQ(x4.widgets[3].h, 1);
    EXPECT_EQ(x3.count, 3);
    EXPECT_TRUE(supportedGeometry(kind, 1, 1));
    EXPECT_FALSE(supportedGeometry(kind, 2, 2));
    EXPECT_EQ(editor.prepare(LayoutInput::memory(raw.data(), raw.size()), "x3", edit), LayoutEditStatus::Invalid);
  }
}

TEST(TimeProjection, KnownZeroMoonIlluminationIsNotEmpty) {
  MoonWidget moon;
  projectMoonWidget({ClockEvidence::Synchronized, 1712615281, 0, {2024, 4, 8, 1, 22, 28, 1}}, {}, moon);
  EXPECT_EQ(moon.status.state, TimeCardState::Ready);
  EXPECT_EQ(moon.illumination, 0);
  EXPECT_EQ(moon.phase, 0);
}
TEST(TimeDrawing, InsufficientPixelsRefuseTheFaceWithoutChangingSavedValues) {
  ClockWidget clock;
  MoonWidget moon;
  projectClockWidget(known(), {}, clock);
  projectMoonWidget(known(), {}, moon);
  for (WidgetBox box : {WidgetBox{224, 90}, WidgetBox{160, 108}}) {
    widgets::TimeDrawing drawing;
    EXPECT_FALSE(widgets::Clock::layoutTime(clock, box, drawing));
    EXPECT_EQ(drawing.count, 0);
    EXPECT_FALSE(widgets::Moon::layoutTime(moon, box, drawing));
    EXPECT_EQ(drawing.count, 0);
  }
  EXPECT_EQ(clock.status.state, TimeCardState::Ready);
  EXPECT_EQ(clock.reading.utcSeconds, 1795789920);
}
TEST(TimeLayout, CompactDefaultPreservesExistingCustomGridCounts) {
  auto input = Layout::defaults();
  std::string raw(input.bytes, '\0');
  ASSERT_TRUE(input.read(input.context, 0, raw.data(), raw.size()));
  Layout parser;
  HomePage page;
  ASSERT_TRUE(parser.project(LayoutInput::memory(raw.data(), raw.size()), "x4", 0, page));
  EXPECT_EQ(page.rows, 6);
  const auto at = raw.find("\"x4\":{\"cols\":2,\"rows\":6}");
  ASSERT_NE(at, std::string::npos);
  raw.replace(at, 24, "\"x4\":{\"cols\":2,\"rows\":7}");
  // Existing custom counts are projected and retained by an unrelated Add.
  ASSERT_TRUE(parser.project(LayoutInput::memory(raw.data(), raw.size()), "x4", 0, page));
  EXPECT_EQ(page.rows, 7);
  LayoutEditor editor;
  LayoutEdit edit;
  edit.kind = LayoutEditKind::Add;
  edit.widget = WidgetKind::Clock;
  edit.revision = 1;
  ASSERT_EQ(editor.prepare(LayoutInput::memory(raw.data(), raw.size()), "x4", edit), LayoutEditStatus::Saved);
  auto output = editor.output();
  std::string saved(output.bytes, '\0');
  ASSERT_TRUE(output.read(output.context, 0, saved.data(), saved.size()));
  ASSERT_TRUE(parser.project(LayoutInput::memory(saved.data(), saved.size()), "x4", 0, page));
  EXPECT_EQ(page.rows, 7);
  ASSERT_TRUE(parser.project(LayoutInput::memory(saved.data(), saved.size()), "x3", 0, page));
  EXPECT_EQ(page.rows, 7);
}
}  // namespace

TEST(TimeHost, TooSmallGeometryHasNoInvisibleRetryTarget) {
 Fixture fixture; ++fixture.time.clock.status.bytes; ++fixture.time.moon.status.bytes;
 WidgetHost host(fixture);host.enter();host.pollWidgets();
 EXPECT_EQ(host.hitWidgetRow(0,{224,90},190,80),255);EXPECT_EQ(host.hitWidgetRow(1,{160,108},120,80),255);
 EXPECT_EQ(fixture.commands,0u);
}

TEST(TimeGeometry, ApprovedMoonUsesFullDrawingAndKeepsStaleFooterSeparate) {
 MoonWidget value;value.status.state=TimeCardState::Ready;value.status.stale=true;widgets::TimeDrawing drawing;ASSERT_TRUE(widgets::Moon::layoutTime(value,{220,104,16,false,true},drawing));EXPECT_EQ(drawing.moon.width,54);EXPECT_EQ(drawing.moon.height,54);for(unsigned i=0;i<drawing.count;++i)EXPECT_LE(drawing.marks[i].cell.y+drawing.marks[i].cell.height,104);EXPECT_FALSE(widgets::Moon::layoutTime(value,{224,96,16,false,false},drawing));
}

TEST(TimeMoonRaster, WhiteAreaMatchesIlluminationAndSideAcrossLunation) {
  // Independent disc-area count: the gallery uses white for illuminated surface.
  struct Phase { uint16_t fraction; int16_t cosine; double white; };
  for (const auto phase : {Phase{0,1000,0}, Phase{125,707,.1465}, Phase{250,0,.5}, Phase{375,-707,.8535}, Phase{500,-1000,1}, Phase{625,-707,.8535}, Phase{750,0,.5}, Phase{875,707,.1465}}) {
    MoonWidget value; value.fraction=phase.fraction;value.terminator=phase.cosine;
    unsigned total=0, shadow=0,left=0,right=0;
    for(int y=-24;y<=24;++y) {
      const auto span=widgets::Moon::shadowRow(value,24,y);
      for(int x=-24;x<=24;++x) if(x*x+y*y<=24*24) {
        ++total;
        if(span.width && x>=span.x && x<span.x+span.width) {++shadow;if(x<0)++left;else if(x>0)++right;}
      }
    }
    EXPECT_NEAR(double(total-shadow)/total,phase.white,.035) << phase.fraction;
    if(phase.fraction==250) EXPECT_GT(left,right*10);
    if(phase.fraction==750) EXPECT_GT(right,left*10);
  }
}
TEST(TimeMoonRaster, InvalidRowsProduceNoPixels) {
 MoonWidget value;EXPECT_EQ(widgets::Moon::shadowRow(value,0,0).width,0);EXPECT_EQ(widgets::Moon::shadowRow(value,24,25).width,0);value.terminator=1001;EXPECT_EQ(widgets::Moon::shadowRow(value,24,0).width,0);
}
