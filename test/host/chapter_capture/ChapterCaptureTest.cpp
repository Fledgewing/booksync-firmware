#include <gtest/gtest.h>

#include <string>

#include "booksync/reader/ChapterCapture.h"
using namespace booksync::reader;
namespace {
ChapterInput route() {
  return {7, "Synthetic book", "River paths", "Across the water", 2, 7, 720, 7200, true, true, true};
}
TEST(ChapterCapture, KnownNativeRouteKeepsMeasuredDurationsAndRemainingCount) {
  ChapterCapture c;
  c.begin(7, 1);
  ASSERT_TRUE(c.publish(route()));
  const auto& v = c.value();
  EXPECT_EQ(v.state, ChapterState::Ready);
  EXPECT_STREQ(v.current.data(), "River paths");
  EXPECT_STREQ(v.next.data(), "Across the water");
  EXPECT_EQ(v.chapter, 2);
  EXPECT_EQ(v.remaining, 4);
  EXPECT_EQ(v.toNextSeconds, 720u);
  EXPECT_EQ(v.toEndSeconds, 7200u);
  EXPECT_TRUE(v.toNextKnown);
  EXPECT_TRUE(v.toEndKnown);
}
TEST(ChapterCapture, MissingPaceNeverBecomesAZeroTimeEstimate) {
  ChapterCapture c;
  c.begin(7, 1);
  auto in = route();
  in.toNextKnown = in.toEndKnown = false;
  in.toNextSeconds = in.toEndSeconds = 0;
  ASSERT_TRUE(c.publish(in));
  EXPECT_EQ(c.value().state, ChapterState::Ready);
  EXPECT_FALSE(c.value().toNextKnown);
  EXPECT_FALSE(c.value().toEndKnown);
}
TEST(ChapterCapture, KnownZeroAtTheEndDiffersFromUnknownPace) {
  ChapterCapture c;
  c.begin(7, 1);
  auto in = route();
  in.chapter = 6;
  in.count = 7;
  in.next = "";
  in.toNextSeconds = in.toEndSeconds = 0;
  ASSERT_TRUE(c.publish(in));
  EXPECT_EQ(c.value().remaining, 0);
  EXPECT_TRUE(c.value().toEndKnown);
  EXPECT_EQ(c.value().toEndSeconds, 0u);
}
TEST(ChapterCapture, NoTocIsExplicitAndDoesNotReuseThePreviousBook) {
  ChapterCapture c;
  c.begin(7, 1);
  ASSERT_TRUE(c.publish(route()));
  c.begin(8, 1);
  EXPECT_EQ(c.value().state, ChapterState::NotSaved);
  EXPECT_EQ(c.value().current[0], 0);
  auto in = route();
  in.open = 8;
  in.tocKnown = false;
  ASSERT_TRUE(c.publish(in));
  EXPECT_EQ(c.value().state, ChapterState::NoContents);
  EXPECT_EQ(c.value().current[0], 0);
}
TEST(ChapterCapture, OtherNativeFormatsAreUnsupportedWithoutChangingTheirReader) {
  for (unsigned format : {0u, 2u, 3u, 99u}) {
    ChapterCapture c;
    c.begin(7, format);
    EXPECT_EQ(c.value().state, ChapterState::Unsupported);
    EXPECT_FALSE(c.publish(route()));
  }
}
TEST(ChapterCapture, ADelayedClosedReaderCannotReplaceANewerOpen) {
  ChapterCapture c;
  c.begin(7, 1);
  auto in = route();
  c.begin(8, 1);
  const auto revision = c.value().revision;
  EXPECT_FALSE(c.publish(in));
  EXPECT_EQ(c.value().open, 8u);
  EXPECT_EQ(c.value().revision, revision);
  EXPECT_EQ(c.value().state, ChapterState::NotSaved);
}
TEST(ChapterCapture, InvalidCountsAndDurationsFailWithoutPartialLabels) {
  for (unsigned mode = 0; mode < 4; ++mode) {
    ChapterCapture c;
    c.begin(7, 1);
    auto in = route();
    if (mode == 0) in.count = 0;
    if (mode == 1) in.chapter = 7;
    if (mode == 2) in.toNextSeconds = 9000000;
    if (mode == 3) in.current = "";
    EXPECT_FALSE(c.publish(in));
    EXPECT_EQ(c.value().state, ChapterState::Unavailable);
    EXPECT_EQ(c.value().current[0], 0);
  }
}
TEST(ChapterCapture, OversizeContentsAndLabelsRefuseTheObservation) {
  for (unsigned mode = 0; mode < 2; ++mode) {
    ChapterCapture c;
    c.begin(7, 1);
    auto in = route();
    std::string longTitle(1000, 'a');
    if (mode == 0)
      in.count = 4097;
    else
      in.current = longTitle;
    EXPECT_FALSE(c.publish(in));
    EXPECT_EQ(c.value().state, ChapterState::Oversize);
  }
}
TEST(ChapterCapture, BoundedUtf8LabelsNeverSplitACharacter) {
  ChapterCapture c;
  c.begin(7, 1);
  auto in = route();
  std::string title;
  for (unsigned n = 0; n < 50; ++n) title += "水";
  in.current = title;
  ASSERT_TRUE(c.publish(in));
  EXPECT_TRUE(c.value().labelsTruncated);
  EXPECT_EQ(std::string(c.value().current.data()), title.substr(0, 93));
}
TEST(ChapterCapture, RetirementRemovesEvidenceAndAdvancesTheRevision) {
  ChapterCapture c;
  c.begin(7, 1);
  ASSERT_TRUE(c.publish(route()));
  const auto revision = c.value().revision;
  c.retire();
  EXPECT_EQ(c.value().state, ChapterState::NotSaved);
  EXPECT_EQ(c.value().open, 0u);
  EXPECT_GT(c.value().revision, revision);
  EXPECT_FALSE(c.publish(route()));
}
}  // namespace
