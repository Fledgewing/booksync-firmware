#include <gtest/gtest.h>
#include <algorithm>
#include <string>
#include "booksync/services/widgets/Glyph.h"
#include "booksync/services/widgets/Moon.h"
#include "booksync/services/LayoutEditor.h"
#include "booksync/services/LayoutGeometry.h"
#include "booksync/ui/widgets/Glyph.h"
using namespace booksync;
namespace {
ClockStatus known() { return {ClockEvidence::Synchronized,1795789920,0,{2026,11,27,5,14,32,0}}; }
class GlyphStates : public testing::TestWithParam<TimeCardState> {};
TEST_P(GlyphStates, EachStateHasIndependentProjectionAndBothProfileGeometry) {
  GlyphWidget out; projectGlyphWidget(known(), true, {GetParam()}, out);
  EXPECT_EQ(out.status.state,GetParam()); EXPECT_EQ(out.codepoint,GetParam()==TimeCardState::Ready ? 'K' : 0u);
  for (bool touch : {false,true}) {
    widgets::TimeDrawing drawing;
    ASSERT_TRUE(widgets::Glyph::layoutTime(out,{224,108,16,false,touch},drawing));
    EXPECT_EQ(drawing.glyph.width>0,GetParam()==TimeCardState::Ready);
    for(unsigned i=0;i<drawing.count;++i) {
      const auto& cell=drawing.marks[i].cell;
      EXPECT_GE(cell.x,0); EXPECT_GE(cell.y,0); EXPECT_LE(cell.x+cell.width,224); EXPECT_LE(cell.y+cell.height,108);
      if(drawing.marks[i].line==widgets::TimeLine::Retry) { EXPECT_GE(cell.width,50); EXPECT_GE(cell.height,50); }
    }
  }
}
INSTANTIATE_TEST_SUITE_P(EveryState,GlyphStates,testing::Values(TimeCardState::Ready,TimeCardState::Empty,
  TimeCardState::NotSaved,TimeCardState::Off,TimeCardState::Unsupported,TimeCardState::Unavailable,TimeCardState::Oversize));
TEST(GlyphProjection, UnknownOrInvalidCivilTimeNeverChoosesAnEpochFallbackLetter) {
  for (auto clock : {ClockStatus{},ClockStatus{ClockEvidence::LiveRtc},ClockStatus{ClockEvidence::Retained,1795789920,0,{2026,2,31}}}) {
    GlyphWidget out; projectGlyphWidget(clock,true,{},out);
    EXPECT_EQ(out.status.state,TimeCardState::NotSaved); EXPECT_EQ(out.codepoint,0u); EXPECT_EQ(out.issue,GlyphIssue::ClockUnavailable);
  }
}
TEST(GlyphProjection, UsesCivilDateAcrossTheUtcBoundary) {
  GlyphWidget out; projectGlyphWidget({ClockEvidence::Retained,1795789920,50400,{2026,11,28,6,4,32}},true,{},out);
  EXPECT_EQ(out.status.state,TimeCardState::Ready); EXPECT_EQ(out.codepoint,'L');
}
TEST(GlyphProjection, AlphabetWrapAndRestartAreDeterministic) {
  GlyphWidget first,again,next;
  projectGlyphWidget({ClockEvidence::Synchronized,1794839520,0,{2026,11,16,1,14,32}},true,{},first);
  projectGlyphWidget({ClockEvidence::Synchronized,1794839520,0,{2026,11,16,1,14,32}},true,{},again);
  projectGlyphWidget({ClockEvidence::Synchronized,1794925920,0,{2026,11,17,2,14,32}},true,{},next);
  EXPECT_EQ(first.codepoint,'Z'); EXPECT_EQ(again.codepoint,'Z'); EXPECT_EQ(next.codepoint,'A');
}
TEST(GlyphProjection, MissingActualFaceNeverOffersAFallbackAsTheSample) {
  GlyphWidget out; projectGlyphWidget(known(),false,{},out);
  EXPECT_EQ(out.status.state,TimeCardState::Unsupported); EXPECT_EQ(out.issue,GlyphIssue::FaceUnavailable); EXPECT_EQ(out.codepoint,0u);
}
TEST(GlyphProjection, DateOutsideDecorRangeIsNamedUnsupported) {
  GlyphWidget out; projectGlyphWidget({ClockEvidence::LiveRtc,4133980800,0,{2101,1,1}},true,{},out);
  EXPECT_EQ(out.status.state,TimeCardState::Unsupported); EXPECT_EQ(out.issue,GlyphIssue::DateUnsupported); EXPECT_EQ(out.codepoint,0u);
}
TEST(GlyphProjection, StaleRetainsActualLetterAndAgeAndOffDoesNotFabricateHistory) {
  GlyphWidget out; projectGlyphWidget(known(),true,{TimeCardState::Ready,2,true,true},out);
  EXPECT_EQ(out.codepoint,'K'); EXPECT_TRUE(out.status.stale); EXPECT_EQ(out.status.ageDays,2u);
  widgets::TimeDrawing drawing; ASSERT_TRUE(widgets::Glyph::layoutTime(out,{224,108},drawing));
  bool age=false; for(unsigned i=0;i<drawing.count;++i) age|=drawing.marks[i].line==widgets::TimeLine::Stale;
  EXPECT_TRUE(age);
  projectGlyphWidget(known(),false,{TimeCardState::Ready,0,false},out); EXPECT_EQ(out.status.state,TimeCardState::Off);
}
class Fixture : public Library {
 public:
  HomePage page{}; TimeWidgetBatch time{}; LibraryWindow publication{}; unsigned batches=0,commands=0; mutable unsigned reads=0,timeReads=0;
  Capabilities capabilities() const override { return {}; } Snapshot snapshot() const override { return {}; }
  HomePage homePage() const override { return page; } bool requestHomePage(uint8_t) override { return true; }
  Subscription subscribe() const override { return {publication.publication,1,true}; }
  bool requestBatch(WindowBatchRequest request) override { ++batches; ++publication.publication; time.requested=request.timeWidgets; return true; }
  void readWindow(LibraryWindow& out) const override { ++reads; out=publication; }
  void readTimeWidgets(TimeWidgetBatch& out) const override { ++timeReads; out=time; }
  CommandStatus submit(Intent) override { ++commands; return CommandStatus::Accepted; }
  Fixture() { page.revision=1; page.count=2; page.rows=6; page.widgets[0]={WidgetKind::Glyph}; page.widgets[1]={WidgetKind::Moon};
    for(auto& place : page.widgets) place.w=place.h=1;
    projectGlyphWidget(known(),true,{},time.glyph); projectMoonWidget(known(),{},time.moon); }
};
TEST(GlyphHost, OneBatchAndOneCopiedValueAreSharedByReadOnlyViews) {
  Fixture f; WidgetHost host(f); host.enter(); ASSERT_TRUE(host.pollWidgets()); EXPECT_EQ(f.time.requested,6);
  for(unsigned repeat=0;repeat<10;++repeat) { EXPECT_EQ(host.view(0).viewState,WidgetState::Known); EXPECT_NE(host.glyphCard(0),nullptr);
    EXPECT_EQ(host.dispatchWidget(0),CommandStatus::Unavailable); EXPECT_EQ(host.hitWidgetRow(0,{224,108},80,40),255); }
  EXPECT_EQ(f.batches,1u); EXPECT_EQ(f.reads,1u); EXPECT_EQ(f.timeReads,1u); EXPECT_EQ(f.commands,0u);
}
TEST(GlyphHost, MalformedOrOversizedPayloadNeverReachesGlyphPaintAndLeavesSiblingUsable) {
  for(bool oversize : {false,true}) {
    Fixture f; if(oversize) ++f.time.glyph.status.bytes; else f.time.glyph.codepoint=0x10ffff;
    WidgetHost host(f); host.enter(); host.pollWidgets();
    EXPECT_EQ(host.view(0).viewState,oversize ? WidgetState::Oversize : WidgetState::Error);
    EXPECT_EQ(host.view(1).viewState,WidgetState::Known); EXPECT_TRUE(host.retryWidget(0)); EXPECT_EQ(f.batches,2u);
    widgets::TimeDrawing drawing; ASSERT_TRUE(widgets::Glyph::layoutTime(f.time.glyph,{224,108},drawing)); EXPECT_EQ(drawing.glyph.width,0);
  }
}
TEST(GlyphHost, X3NeverRequestsOrExposesTheNewLetter) {
  Fixture f; f.page.textOnly=true; f.page.count=1; WidgetHost host(f); host.enter(); host.pollWidgets();
  EXPECT_EQ(f.time.requested,0); EXPECT_EQ(f.timeReads,0u); EXPECT_EQ(host.glyphCard(0),nullptr); EXPECT_EQ(host.dispatchWidget(0),CommandStatus::Unavailable);
}
TEST(GlyphLayout, OnlyApprovedMainSizeInstallsAndOtherDeviceBytesArePreserved) {
  auto input=Layout::defaults(); std::string original(input.bytes,'\0'); ASSERT_TRUE(input.read(input.context,0,original.data(),original.size()));
  LayoutEdit edit;edit.kind=LayoutEditKind::Add;edit.widget=WidgetKind::Glyph;edit.revision=1;
  LayoutEditor editor; ASSERT_EQ(editor.prepare(LayoutInput::memory(original.data(),original.size()),"x4",edit),LayoutEditStatus::Saved);
  auto output=editor.output();std::string saved(output.bytes,'\0');ASSERT_TRUE(output.read(output.context,0,saved.data(),saved.size()));
  Layout parser;HomePage x4,x3;ASSERT_TRUE(parser.project(LayoutInput::memory(saved.data(),saved.size()),"x4",0,x4));
  ASSERT_TRUE(parser.project(LayoutInput::memory(saved.data(),saved.size()),"x3",0,x3));
  EXPECT_EQ(x4.count,4);EXPECT_EQ(x4.widgets[3].kind,WidgetKind::Glyph); EXPECT_EQ(x3.count,3);
  EXPECT_TRUE(supportedGeometry(WidgetKind::Glyph,1,1));EXPECT_FALSE(supportedGeometry(WidgetKind::Glyph,2,2));
  EXPECT_EQ(editor.prepare(LayoutInput::memory(original.data(),original.size()),"x3",edit),LayoutEditStatus::Invalid);
}
}
