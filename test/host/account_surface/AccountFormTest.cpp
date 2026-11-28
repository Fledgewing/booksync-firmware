#include <gtest/gtest.h>

#include "booksync/ui/account/AccountForm.h"
using namespace booksync;
namespace {
class FormLibrary final : public Library {
 public:
  AccountFormView formValue{};
  mutable unsigned reads = 0;
  AccountView separateAccount{};
  bool useSeparateAccount = false;
  unsigned writes = 0, submissions = 0;
  FormLibrary() {
    formValue.form = 1;
    formValue.account.ticket = formValue.origin = {8, 2};
  }
  Capabilities capabilities() const override { return {}; }
  Snapshot snapshot() const override { return {}; }
  CommandStatus submit(Intent) override { return CommandStatus::Unsupported; }
  AccountView accountView() const override {
    ++reads;
    return useSeparateAccount ? separateAccount : formValue.account;
  }
  void readAccountForm(AccountFormView& out) const override {
    ++reads;
    out = formValue;
  }
  CommandStatus accountAction(AccountFormRequest r) override {
    ++writes;
    if (r.action == AccountAction::Begin)
      ++formValue.form;
    else if (r.action == AccountAction::Manual)
      formValue.scene = AccountScene::Manual;
    else if (r.action == AccountAction::Phone)
      formValue.scene = AccountScene::Phone;
    else if (r.action == AccountAction::Check)
      formValue.scene = AccountScene::Wait;
    ++formValue.revision;
    return CommandStatus::Accepted;
  }
  CommandStatus submitAccount(AccountFormRequest, AccountEntry& e) override {
    EXPECT_EQ(e.password[0], 'x');
    e.clear();
    ++submissions;
    formValue.scene = AccountScene::Busy;
    ++formValue.revision;
    return CommandStatus::Accepted;
  }
};
TEST(AccountForm, AllApprovedScenesHaveBoundedActionsAndInitialFocus) {
  FormLibrary f;
  for (unsigned scene = 0; scene < 11; ++scene) {
    f.formValue.scene = static_cast<AccountScene>(scene);
    AccountForm ui(f);
    ui.enterForm();
    EXPECT_TRUE(ui.live());
    EXPECT_EQ(ui.focus(), 0);
    EXPECT_GE(ui.actions(), 1);
    EXPECT_LE(ui.actions(), 3);
    ui.stepForm(-1);
    EXPECT_EQ(ui.focus(), 0);
    for (int i = 0; i < 10; ++i) ui.stepForm(1);
    EXPECT_EQ(ui.focus(), ui.actions() - 1);
    ui.retireForm();
  }
}
TEST(AccountForm, ManualInputClearsAfterSubmissionAndCancel) {
  FormLibrary f;
  f.formValue.scene = AccountScene::Manual;
  AccountForm ui(f);
  ui.enterForm();
  ui.advanceFormField();
  ui.advanceFormField();
  ASSERT_TRUE(ui.appendForm('x'));
  ui.chooseForm(1);
  EXPECT_EQ(f.submissions, 1);
  EXPECT_EQ(ui.entry().password[0], 0);
  ui.retireForm();
  EXPECT_EQ(ui.entry().server[0], 0);
  EXPECT_EQ(ui.entry().user[0], 0);
}
TEST(AccountForm, ClosedFormNeverReadsOrDispatchesLateSuccess) {
  FormLibrary f;
  AccountForm ui(f);
  ui.enterForm();
  ui.retireForm();
  const auto reads = f.reads, writes = f.writes;
  f.formValue.scene = AccountScene::In;
  ++f.formValue.account.ticket.epoch;
  EXPECT_FALSE(ui.pollForm());
  ui.chooseForm(0);
  ui.stepForm(1);
  ui.appendForm('x');
  EXPECT_EQ(f.reads, reads);
  EXPECT_EQ(f.writes, writes);
}
TEST(AccountForm, ExternalEpochAndABAInvalidateBeforeInputDispatch) {
  FormLibrary f;
  AccountForm ui(f);
  ui.enterForm();
  f.formValue.account.ticket.epoch += 2;
  ui.chooseForm(0);
  EXPECT_FALSE(ui.live());
  EXPECT_EQ(f.writes, 1);
}
TEST(AccountForm, CorrelatedSuccessMayAdoptEpochButAnotherFormCannot) {
  FormLibrary f;
  f.formValue.scene = AccountScene::Busy;
  AccountForm ui(f);
  ui.enterForm();
  f.formValue.scene = AccountScene::In;
  ++f.formValue.account.ticket.epoch;
  ++f.formValue.revision;
  EXPECT_TRUE(ui.pollForm());
  EXPECT_TRUE(ui.live());
  EXPECT_EQ(ui.view().account.ticket.epoch, f.formValue.account.ticket.epoch);
  ++f.formValue.form;
  ++f.formValue.revision;
  EXPECT_TRUE(ui.pollForm());
  EXPECT_FALSE(ui.live());
}
TEST(AccountForm, PhoneCheckIsExplicitAndTypingClearsOldLink) {
  FormLibrary f;
  f.formValue.scene = AccountScene::Phone;
  AccountForm ui(f);
  ui.enterForm();
  const auto writes = f.writes;
  EXPECT_FALSE(ui.pollForm());
  EXPECT_EQ(f.writes, writes);
  ui.chooseForm(0);
  EXPECT_EQ(f.formValue.scene, AccountScene::Wait);
  ui.retireForm();
  f.formValue.scene = AccountScene::Phone;
  ui.enterForm();
  ui.chooseForm(1);
  EXPECT_EQ(ui.view().scene, AccountScene::Manual);
}
TEST(AccountForm, EntryCapacityAndLeaveEraseEveryField) {
  FormLibrary f;
  f.formValue.scene = AccountScene::Manual;
  AccountForm ui(f);
  ui.enterForm();
  for (unsigned field = 0; field < 3; ++field) {
    const unsigned capacity = field == 0 ? 512 : field == 1 ? 100 : 1024;
    for (unsigned i = 0; i < capacity; ++i) ASSERT_TRUE(ui.appendForm('x'));
    EXPECT_FALSE(ui.appendForm('x'));
    EXPECT_FALSE(ui.appendForm('\n'));
    ui.eraseForm();
    EXPECT_TRUE(ui.appendForm('x'));
    ui.advanceFormField();
  }
  ui.retireForm();
  for (const char c : ui.entry().server) EXPECT_EQ(c, 0);
  for (const char c : ui.entry().user) EXPECT_EQ(c, 0);
  for (const char c : ui.entry().password) EXPECT_EQ(c, 0);
}
TEST(AccountForm, UncorrelatedSuccessCannotAdoptNewAccount) {
  FormLibrary f;
  f.formValue.scene = AccountScene::Busy;
  AccountForm ui(f);
  ui.enterForm();
  ++f.formValue.origin.epoch;
  ++f.formValue.account.ticket.epoch;
  f.formValue.scene = AccountScene::In;
  ++f.formValue.revision;
  EXPECT_TRUE(ui.pollForm());
  EXPECT_FALSE(ui.live());
}
TEST(AccountForm, ResultMustMatchCurrentLibraryAccountBeforeAnyPaint) {
  FormLibrary f;
  f.formValue.scene = AccountScene::Busy;
  AccountForm ui(f);
  ui.enterForm();
  f.separateAccount = f.formValue.account;
  f.useSeparateAccount = true;
  ++f.formValue.account.ticket.epoch;
  f.formValue.scene = AccountScene::In;
  ++f.formValue.revision;
  EXPECT_TRUE(ui.pollForm());
  EXPECT_FALSE(ui.live());
}
TEST(AccountForm, DecreasingRevisionRetiresWithoutDisplayingAnOldResult) {
  FormLibrary f;
  f.formValue.revision = 20;
  AccountForm ui(f);
  ui.enterForm();
  --f.formValue.revision;
  EXPECT_TRUE(ui.pollForm());
  EXPECT_FALSE(ui.live());
  const auto reads = f.reads;
  EXPECT_FALSE(ui.pollForm());
  EXPECT_EQ(f.reads, reads);
}
TEST(AccountForm, UnsupportedLibraryErasesWriteOnlyEntry) {
  class Unavailable final : public Library {
    Capabilities capabilities() const override { return {}; }
    Snapshot snapshot() const override { return {}; }
    CommandStatus submit(Intent) override { return CommandStatus::Unavailable; }
  } f;
  AccountEntry input;
  input.password.fill('x');
  EXPECT_EQ(f.submitAccount({}, input), CommandStatus::Unavailable);
  for (const char c : input.password) EXPECT_EQ(c, 0);
}
}  // namespace
