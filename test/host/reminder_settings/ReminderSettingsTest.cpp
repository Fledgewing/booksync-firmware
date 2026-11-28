#include <gtest/gtest.h>

#include <cstring>

#include "booksync/ui/reminders/Settings.h"
using namespace booksync;
namespace {
class SettingsLibrary final : public Library {
 public:
  ReminderList value{};
  unsigned reads = 0, writes = 0, connects = 0, retries = 0;
  ReminderMutation mutation{};
  ReminderResult command{ReminderStatus::Pending};
  Capabilities capabilities() const override { return {}; }
  Snapshot snapshot() const override { return {}; }
  CommandStatus submit(Intent intent) override {
    if (intent == Intent::ConnectReminderTime) ++connects;
    return CommandStatus::Accepted;
  }
  void remindersList(ReminderList& out) const override {
    ++const_cast<SettingsLibrary*>(this)->reads;
    out = value;
  }
  ReminderResult remindersMutate(const ReminderMutation& m) override {
    ++writes;
    mutation = m;
    return {ReminderStatus::Pending};
  }
  ReminderResult remindersResult() const override { return command; }
  bool remindersRetry() override {
    ++retries;
    return true;
  }
};
class ReminderSettingsTest : public ::testing::Test {
 protected:
  SettingsLibrary library;
  ReminderSettings model{library};
  void SetUp() override {
    auto& value = library.value;
    value.storage = ReminderStorage::Ready;
    value.count = 1;
    value.nextMutationKey = 17;
    value.rows[0].id = 1;
    value.rows[0].revision = 3;
    value.rows[0].fields.kind = ReminderKind::Interval;
    value.rows[0].fields.intervalMinutes = 20;
    model.enterReminders();
  }
  void timeRow() {
    auto& r = library.value.rows[1];
    r.id = 2;
    r.revision = 4;
    r.fields.enabled = true;
    r.fields.minute = 1260;
    r.fields.days = 127;
    library.value.count = 2;
    model.enterReminders();
  }
};
TEST_F(ReminderSettingsTest, PaintMemoAndDraftNeverReadOrWriteLibrary) {
  for (unsigned i = 0; i < 20; ++i) {
    (void)model.memo();
    (void)model.draft();
    (void)model.pollReminders();
  }
  EXPECT_EQ(library.reads, 1);
  EXPECT_EQ(library.writes, 0);
}
TEST_F(ReminderSettingsTest, ChildDoneOnlyChangesDraftAndExplicitSavePersistsOnce) {
  ASSERT_TRUE(model.addReminderDraft());
  model.openReminderChild(ReminderScreen::Time);
  model.setReminderMinute(1330);
  ASSERT_TRUE(model.finishReminderChild());
  EXPECT_EQ(model.draft().draftMutation.fields.minute, 1330);
  EXPECT_EQ(library.writes, 0);
  ASSERT_TRUE(model.saveReminderDraft());
  EXPECT_EQ(library.writes, 1);
  EXPECT_EQ(library.mutation.key, 17);
  EXPECT_EQ(library.mutation.operation, ReminderOperation::Add);
  EXPECT_EQ(model.screen(), ReminderScreen::Saving);
  EXPECT_FALSE(model.saveReminderDraft());
  EXPECT_EQ(library.writes, 1);
}
TEST_F(ReminderSettingsTest, BackDiscardsChildThenEditorWithoutSaving) {
  model.addReminderDraft();
  model.openReminderChild(ReminderScreen::Time);
  model.setReminderMinute(1);
  model.backReminders();
  EXPECT_EQ(model.draft().draftMutation.fields.minute, 1260);
  model.backReminders();
  EXPECT_EQ(model.screen(), ReminderScreen::List);
  EXPECT_EQ(library.writes, 0);
}
TEST_F(ReminderSettingsTest, DaysRequireAtLeastOneAndPresetsRemainSevenBit) {
  model.addReminderDraft();
  model.openReminderChild(ReminderScreen::Days);
  model.customReminderDays();
  model.setReminderDays(0);
  EXPECT_FALSE(model.finishReminderChild());
  EXPECT_EQ(model.error(), ReminderStatus::Invalid);
  EXPECT_EQ(library.writes, 0);
  model.setReminderDays(31);
  ASSERT_TRUE(model.finishReminderChild());
  EXPECT_EQ(model.draft().draftMutation.fields.days, 31);
}
TEST_F(ReminderSettingsTest, MessageAcceptsSixtyUnicodeCharactersAndRejectsOneMore) {
  model.addReminderDraft();
  model.openReminderChild(ReminderScreen::Message);
  for (unsigned i = 0; i < 60; ++i)
    ASSERT_TRUE(model.appendReminderMessage({{char(0xf0), char(0x9f), char(0x98), char(0x80), 0}}));
  EXPECT_EQ(std::strlen(model.draft().child.message), 240);
  EXPECT_FALSE(model.appendReminderMessage({{'a', 0}}));
  model.eraseReminderMessage();
  EXPECT_EQ(std::strlen(model.draft().child.message), 236);
  EXPECT_TRUE(model.appendReminderMessage({{'a', 0}}));
  EXPECT_EQ(library.writes, 0);
}
TEST_F(ReminderSettingsTest, MessageRejectsNewlinesControlsMalformedAndOverlongUtf8) {
  model.addReminderDraft();
  model.openReminderChild(ReminderScreen::Message);
  EXPECT_FALSE(model.appendReminderMessage({{'\n', 0}}));
  EXPECT_FALSE(model.appendReminderMessage({{char(0xe2), char(0x80), char(0xa8), 0}}));
  EXPECT_FALSE(model.appendReminderMessage({{char(0xe2), char(0x80), char(0xa9), 0}}));
  EXPECT_FALSE(model.appendReminderMessage({{char(0xc0), char(0x80), 0}}));
  EXPECT_FALSE(model.appendReminderMessage({{char(0xed), char(0xa0), char(0x80), 0}}));
  EXPECT_FALSE(model.appendReminderMessage({{char(0xe2), char(0x82), 0}}));
  EXPECT_STREQ(model.draft().child.message, "");
}
TEST_F(ReminderSettingsTest, AllIntervalsEditSingletonWithoutAddDeleteOrTimeFields) {
  ASSERT_TRUE(model.editReminderDraft(0));
  model.openReminderChild(ReminderScreen::Time);
  EXPECT_EQ(model.screen(), ReminderScreen::Editor);
  model.requestReminderDelete();
  EXPECT_EQ(model.screen(), ReminderScreen::Editor);
  model.openReminderChild(ReminderScreen::Interval);
  for (uint8_t m : {20, 30, 45, 60}) {
    model.setReminderInterval(m);
    EXPECT_EQ(model.draft().child.intervalMinutes, m);
  }
  model.setReminderInterval(21);
  EXPECT_EQ(model.draft().child.intervalMinutes, 60);
  model.finishReminderChild();
  ASSERT_TRUE(model.saveReminderDraft());
  EXPECT_EQ(library.mutation.id, 1);
  EXPECT_EQ(library.mutation.operation, ReminderOperation::Edit);
}
TEST_F(ReminderSettingsTest, EightTimeLimitDoesNotHideIntervalAndListPagesFour) {
  library.value.count = 9;
  for (unsigned i = 1; i < 9; ++i) {
    library.value.rows[i].id = i + 1;
    library.value.rows[i].fields.days = 127;
  }
  model.enterReminders();
  EXPECT_EQ(model.pages(), 3);
  EXPECT_FALSE(model.addReminderDraft());
  model.setReminderPage(2);
  EXPECT_EQ(model.page(), 2);
  model.setReminderPage(0);
  EXPECT_TRUE(model.editReminderDraft(0));
}
TEST_F(ReminderSettingsTest, ManagedRowsCannotToggleSaveOrDeleteEvenWithLocalIds) {
  timeRow();
  library.value.rows[1].source = ReminderSource::Managed;
  library.value.rows[1].editable = false;
  model.enterReminders();
  model.editReminderDraft(1);
  model.toggleReminderDraft();
  model.requestReminderDelete();
  EXPECT_EQ(model.screen(), ReminderScreen::Editor);
  EXPECT_FALSE(model.saveReminderDraft());
  EXPECT_EQ(library.writes, 0);
}
TEST_F(ReminderSettingsTest, DeleteRequiresExplicitConfirmationAndBackChangesNothing) {
  timeRow();
  model.editReminderDraft(1);
  model.requestReminderDelete();
  EXPECT_EQ(library.writes, 0);
  model.backReminders();
  EXPECT_EQ(library.writes, 0);
  model.requestReminderDelete();
  ASSERT_TRUE(model.confirmReminderDelete());
  EXPECT_EQ(library.writes, 1);
  EXPECT_EQ(library.mutation.operation, ReminderOperation::Delete);
}
TEST_F(ReminderSettingsTest, StorageFailureKeepsExactUnsavedDraftAndMutationKey) {
  model.addReminderDraft();
  model.openReminderChild(ReminderScreen::Time);
  model.setReminderMinute(15);
  model.finishReminderChild();
  model.saveReminderDraft();
  library.command = {ReminderStatus::StorageError};
  ASSERT_TRUE(model.pollReminders());
  EXPECT_EQ(model.screen(), ReminderScreen::Editor);
  EXPECT_EQ(model.draft().draftMutation.fields.minute, 15);
  EXPECT_EQ(model.draft().draftMutation.key, 17);
  EXPECT_EQ(library.reads, 1);
  EXPECT_TRUE(model.saveReminderDraft());
  EXPECT_EQ(library.mutation.key, 17);
}
TEST_F(ReminderSettingsTest, UnreadablePreservesDataAndRetryReacquiresAfterWorker) {
  library.value.storage = ReminderStorage::Unreadable;
  model.enterReminders();
  EXPECT_FALSE(model.addReminderDraft());
  ASSERT_TRUE(model.retryReminders());
  EXPECT_EQ(library.retries, 1);
  library.value.storage = ReminderStorage::Recovered;
  EXPECT_TRUE(model.pollReminders());
  EXPECT_EQ(model.memo().storage, ReminderStorage::Recovered);
}
TEST_F(ReminderSettingsTest, UnknownTimeAllowsEditsAndConnectIsExplicit) {
  EXPECT_FALSE(model.memo().clockKnown);
  EXPECT_EQ(library.connects, 0);
  model.connectReminderTime();
  EXPECT_EQ(library.connects, 1);
  EXPECT_TRUE(model.addReminderDraft());
  model.connectReminderTime();
  EXPECT_EQ(library.connects, 1);
}
TEST_F(ReminderSettingsTest, RetiredViewNeverAdoptsLateSaveButDeviceCommandRemains) {
  model.addReminderDraft();
  model.saveReminderDraft();
  model.backReminders();
  library.command = {ReminderStatus::Saved};
  EXPECT_FALSE(model.pollReminders());
  EXPECT_EQ(library.writes, 1);
  EXPECT_EQ(library.reads, 1);
}
}  // namespace
