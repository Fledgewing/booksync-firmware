#include <gtest/gtest.h>

#include "booksync/ui/reminders/DueSheet.h"
using namespace booksync;
namespace {
class SheetLibrary final : public Library {
 public:
  unsigned writes = 0, reads = 0, retired = 0;
  ReminderAction action = ReminderAction::GotIt;
  ReminderResult response{ReminderStatus::Pending};
  Capabilities capabilities() const override { return {}; }
  Snapshot snapshot() const override { return {}; }
  CommandStatus submit(Intent) override { return CommandStatus::Unavailable; }
  ReminderResult remindersRespond(uint64_t, uint64_t, ReminderAction a) override {
    ++writes;
    action = a;
    return {ReminderStatus::Pending};
  }
  ReminderResult remindersResult() const override {
    ++const_cast<SheetLibrary*>(this)->reads;
    return response;
  }
  void remindersRetire(uint64_t) override { ++retired; }
};
class ReminderSheetTest : public ::testing::Test {
 protected:
  SheetLibrary library;
  ReminderDueSheet sheet{library};
  ReminderDue due{};
  ReminderClaim sheetClaim{ReminderSource::Local, ReminderKind::Time, 2, 1, 1, 7, 0, 0};
  void SetUp() override {
    due.id = 2;
    due.revision = 1;
    sheet.enterSheet(due, sheetClaim, true);
  }
  void release() { sheet.sheetInput(ReminderSheetInput::ReleaseAll); }
  void select() {
    sheet.sheetInput(ReminderSheetInput::SelectDown);
    sheet.sheetInput(ReminderSheetInput::SelectUp);
  }
};
TEST_F(ReminderSheetTest, OpeningAndHeldSelectCannotActivate) {
  sheet.sheetInput(ReminderSheetInput::SelectUp);
  EXPECT_EQ(library.writes, 0);
  release();
  sheet.sheetInput(ReminderSheetInput::SelectUp);
  EXPECT_EQ(library.writes, 0);
  select();
  EXPECT_EQ(library.action, ReminderAction::GotIt);
  EXPECT_EQ(library.writes, 1);
}
TEST_F(ReminderSheetTest, PageTurnBackAndOutsideDismissEvenWithSleepFocused) {
  release();
  sheet.sheetInput(ReminderSheetInput::FocusNext);
  sheet.sheetInput(ReminderSheetInput::FocusNext);
  ASSERT_EQ(sheet.view().focus, 2);
  sheet.sheetInput(ReminderSheetInput::PageTurn);
  EXPECT_EQ(library.action, ReminderAction::GotIt);
  EXPECT_EQ(library.writes, 1);
}
TEST_F(ReminderSheetTest, FreshDistinctFocusAndSelectAloneRequestsSleep) {
  release();
  sheet.sheetInput(ReminderSheetInput::FocusNext);
  sheet.sheetInput(ReminderSheetInput::FocusNext);
  select();
  EXPECT_EQ(library.action, ReminderAction::Sleep);
  EXPECT_EQ(library.writes, 1);
}
TEST_F(ReminderSheetTest, DragOntoOrOffSleepNeverSelectsIt) {
  release();
  sheet.sheetInput(ReminderSheetInput::TouchDown, 255);
  sheet.sheetInput(ReminderSheetInput::TouchUp, 2);
  sheet.sheetInput(ReminderSheetInput::TouchDown, 2);
  sheet.sheetInput(ReminderSheetInput::TouchUp, 255);
  EXPECT_EQ(library.writes, 0);
  sheet.sheetInput(ReminderSheetInput::TouchDown, 2);
  sheet.sheetInput(ReminderSheetInput::TouchUp, 2);
  EXPECT_EQ(library.action, ReminderAction::Sleep);
  EXPECT_EQ(library.writes, 1);
}
TEST_F(ReminderSheetTest, UnavailableOrFailedSleepKeepsReadingAndGotItFocus) {
  sheet.retireSheet();
  sheet.enterSheet(due, sheetClaim, false);
  release();
  sheet.sheetInput(ReminderSheetInput::FocusNext);
  sheet.sheetInput(ReminderSheetInput::FocusNext);
  select();
  EXPECT_EQ(library.writes, 0);
  EXPECT_TRUE(sheet.view().open);
  EXPECT_EQ(sheet.view().focus, 0);
  EXPECT_EQ(sheet.view().error, ReminderSheetError::Sleep);
}
TEST_F(ReminderSheetTest, RetiredCallbackNeverReadsOrRepaints) {
  release();
  select();
  sheet.retireSheet();
  const auto revision = sheet.revision();
  library.response = {ReminderStatus::Saved};
  EXPECT_FALSE(sheet.pollSheet());
  EXPECT_EQ(library.reads, 0);
  EXPECT_EQ(sheet.revision(), revision);
}
TEST_F(ReminderSheetTest, ClockTicksAndNoPendingResultDoNotChangeRevision) {
  const auto revision = sheet.revision();
  for (unsigned i = 0; i < 100; ++i) EXPECT_FALSE(sheet.pollSheet());
  EXPECT_EQ(sheet.revision(), revision);
  EXPECT_EQ(library.reads, 0);
  EXPECT_EQ(library.writes, 0);
}
TEST_F(ReminderSheetTest, EyeRestHasDoneAndNoSleepOrSnooze) {
  due.kind = ReminderKind::Interval;
  sheetClaim.kind = ReminderKind::Interval;
  sheet.retireSheet();
  sheet.enterSheet(due, sheetClaim, false);
  release();
  sheet.sheetInput(ReminderSheetInput::FocusNext);
  sheet.sheetInput(ReminderSheetInput::PageTurn);
  EXPECT_EQ(library.action, ReminderAction::Done);
}
TEST_F(ReminderSheetTest, ManagedTurnOffIsDisabledAndDoneRemainsAvailable) {
  due.kind = ReminderKind::Interval;
  due.source = ReminderSource::Managed;
  sheetClaim.source = ReminderSource::Managed;
  sheetClaim.kind = ReminderKind::Interval;
  sheet.retireSheet();
  sheet.enterSheet(due, sheetClaim, false);
  release();
  sheet.sheetInput(ReminderSheetInput::FocusNext);
  select();
  EXPECT_EQ(library.writes, 0);
  sheet.sheetInput(ReminderSheetInput::Back);
  EXPECT_EQ(library.action, ReminderAction::Done);
}
TEST_F(ReminderSheetTest, ExplicitTurnOffFailureRetainsDoneFocusAndEnabledView) {
  due.kind = ReminderKind::Interval;
  sheetClaim.kind = ReminderKind::Interval;
  sheet.retireSheet();
  sheet.enterSheet(due, sheetClaim, false);
  release();
  sheet.sheetInput(ReminderSheetInput::FocusNext);
  select();
  EXPECT_EQ(library.action, ReminderAction::TurnOff);
  library.response = {ReminderStatus::StorageError};
  EXPECT_TRUE(sheet.pollSheet());
  EXPECT_TRUE(sheet.view().open);
  EXPECT_EQ(sheet.view().focus, 0);
  EXPECT_EQ(sheet.view().error, ReminderSheetError::Save);
}
}  // namespace

namespace {
TEST_F(ReminderSheetTest, HeldSelectCanceledByFocusCannotSelectSleep) {
  release();
  sheet.sheetInput(ReminderSheetInput::SelectDown);
  sheet.sheetInput(ReminderSheetInput::FocusNext);
  sheet.sheetInput(ReminderSheetInput::FocusNext);
  sheet.sheetInput(ReminderSheetInput::SelectUp);
  EXPECT_EQ(library.writes, 0);
  select();
  EXPECT_EQ(library.action, ReminderAction::Sleep);
}
TEST_F(ReminderSheetTest, VisibleSheetCannotBeReplacedMidInput) {
  const auto revision = sheet.revision();
  auto other = due;
  other.id = 3;
  auto otherClaim = sheetClaim;
  otherClaim.id = 3;
  EXPECT_FALSE(sheet.enterSheet(other, otherClaim, true));
  EXPECT_EQ(sheet.view().due.id, 2);
  EXPECT_EQ(sheet.revision(), revision);
}
TEST_F(ReminderSheetTest, NativeSleepFailureReturnsToGotItWithoutClosing) {
  release();
  sheet.sheetInput(ReminderSheetInput::FocusNext);
  sheet.sheetInput(ReminderSheetInput::FocusNext);
  select();
  library.response = {ReminderStatus::Unavailable};
  EXPECT_TRUE(sheet.pollSheet());
  EXPECT_TRUE(sheet.view().open);
  EXPECT_EQ(sheet.view().focus, 0);
  EXPECT_EQ(sheet.view().error, ReminderSheetError::Sleep);
}
}  // namespace
