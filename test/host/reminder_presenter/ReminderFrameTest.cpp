#include <gtest/gtest.h>

#include "booksync/system/ReminderFrame.h"
using namespace booksync;
TEST(ReminderFrame, LoadedBookDoesNotProveAVisiblePage) {
  ReminderFrame frame;
  EXPECT_FALSE(frame.ready());
  frame.beginRender();
  frame.finishRender(true);
  EXPECT_FALSE(frame.ready());
}
TEST(ReminderFrame, OnlyAcceptedNativePageEstablishesReadingVisibility) {
  ReminderFrame frame;
  frame.beginRender();
  frame.readingPage();
  frame.finishRender(true);
  EXPECT_TRUE(frame.ready());
}
TEST(ReminderFrame, AutomaticNoopRefreshKeepsTheSamePageWithoutActivity) {
  ReminderFrame frame;
  frame.beginRender();
  frame.readingPage();
  frame.finishRender(true);
  frame.beginRender();
  EXPECT_TRUE(frame.ready());
  frame.finishRender(true);
  EXPECT_TRUE(frame.ready());
}
TEST(ReminderFrame, LongLoadingOrErrorFrameBlocksImmediately) {
  ReminderFrame frame;
  frame.beginRender();
  frame.readingPage();
  frame.finishRender(true);
  frame.beginRender();
  frame.nonReadingFrame();
  EXPECT_FALSE(frame.ready());
  frame.finishRender(true);
  EXPECT_FALSE(frame.ready());
}
TEST(ReminderFrame, LaterAcceptedPageRestoresAfterLoading) {
  ReminderFrame frame;
  frame.beginRender();
  frame.nonReadingFrame();
  frame.readingPage();
  frame.finishRender(true);
  EXPECT_TRUE(frame.ready());
}
TEST(ReminderFrame, NavigationOrReminderRequiresAFreshParentFrameOnReturn) {
  ReminderFrame frame;
  frame.beginRender();
  frame.readingPage();
  frame.finishRender(true);
  frame.block();
  EXPECT_FALSE(frame.ready());
  frame.beginRender();
  frame.finishRender(true);
  EXPECT_FALSE(frame.ready());
  frame.beginRender();
  frame.readingPage();
  frame.finishRender(true);
  EXPECT_TRUE(frame.ready());
}
TEST(ReminderFrame, HomeAndMenusBecomeVisibleAfterTheirNativeRenderOnly) {
  ReminderFrame frame;
  EXPECT_FALSE(frame.ready());
  frame.beginRender();
  frame.finishRender(false);
  EXPECT_TRUE(frame.ready());
}

TEST(ReminderFrame, InlineModalRetiresProofUntilAnOrdinaryFrameReturns) {
  ReminderFrame frame;
  frame.beginRender();
  frame.finishRender(false);
  ASSERT_TRUE(frame.ready());
  frame.beginRender();
  frame.finishRender(false, false);
  EXPECT_FALSE(frame.ready());
  // Closing an inline modal changes main-loop state before its replacement
  // paint. That state transition alone cannot prove an ordinary visible frame.
  EXPECT_FALSE(frame.ready());
  frame.beginRender();
  frame.finishRender(false, true);
  EXPECT_TRUE(frame.ready());
}
