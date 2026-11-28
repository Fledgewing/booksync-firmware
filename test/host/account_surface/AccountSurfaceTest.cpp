#include <gtest/gtest.h>

#include "booksync/ui/account/ServiceChoice.h"
using namespace booksync;
namespace {
class Fake final : public Library {
 public:
  AccountView value{};
  mutable unsigned reads = 0;
  unsigned commands = 0;
  ServiceSelection received{};
  CommandStatus result = CommandStatus::Accepted;
  Fake() {
    value.service = 7;
    value.available = true;
    value.serviceLabel = {'S', 'e', 'r', 'v', 'i', 'c', 'e'};
  }
  Capabilities capabilities() const override { return {}; }
  Snapshot snapshot() const override { return {}; }
  CommandStatus submit(Intent) override { return CommandStatus::Unsupported; }
  AccountView accountView() const override {
    ++reads;
    return value;
  }
  CommandStatus selectService(ServiceSelection selection) override {
    ++commands;
    received = selection;
    return result;
  }
};
TEST(AccountSurface, ContinueSubmitsOpaqueServiceAndEpoch) {
  Fake f;
  f.value.ticket = {3, 91};
  ServiceChoice ui(f);
  ui.enter();
  EXPECT_EQ(ui.focus(), 0);
  ui.chooseChoice(0);
  ui.chooseChoice(2);
  EXPECT_EQ(f.commands, 1);
  EXPECT_EQ(f.received.service, 7);
  EXPECT_EQ(f.received.ticket.owner, 3);
  EXPECT_EQ(f.received.ticket.epoch, 91);
  EXPECT_FALSE(ui.live());
}
TEST(AccountSurface, LocalBooksAndLaterNeedNoSignIn) {
  Fake f;
  ServiceChoice ui(f);
  ui.enter();
  ui.chooseChoice(1);
  ui.chooseChoice(2);
  EXPECT_EQ(f.received.service, 0);
  EXPECT_EQ(f.commands, 1);
  ui.enter();
  ui.chooseChoice(3);
  EXPECT_FALSE(ui.live());
  EXPECT_EQ(f.commands, 1);
}
TEST(AccountSurface, ClosedFormNeverReadsOrRepaints) {
  Fake f;
  ServiceChoice ui(f);
  ui.enter();
  ui.retireChoice();
  const auto reads = f.reads;
  f.value.signedIn = true;
  ++f.value.ticket.epoch;
  EXPECT_FALSE(ui.pollChoice());
  ui.chooseChoice(2);
  ui.stepChoice(1);
  EXPECT_EQ(f.reads, reads);
  EXPECT_EQ(f.commands, 0);
}
TEST(AccountSurface, SameOwnerNewEpochRetiresAndRejectsSubmit) {
  Fake f;
  f.value.ticket = {1, 5};
  ServiceChoice ui(f);
  ui.enter();
  f.value.ticket = {1, 7};
  ui.chooseChoice(2);
  EXPECT_FALSE(ui.live());
  EXPECT_EQ(f.commands, 0);
  EXPECT_EQ(ui.view().service, 0);
}
TEST(AccountSurface, AccountChangeClosesWithoutOpeningAnotherScreen) {
  Fake f;
  ServiceChoice ui(f);
  ui.enter();
  f.value.ticket = {2, 3};
  EXPECT_TRUE(ui.pollChoice());
  EXPECT_FALSE(ui.live());
  EXPECT_FALSE(ui.pollChoice());
}
TEST(AccountSurface, RejectedSelectionRemainsOpenAndNoSuccessIsInvented) {
  Fake f;
  f.result = CommandStatus::Unavailable;
  ServiceChoice ui(f);
  ui.enter();
  ui.chooseChoice(2);
  EXPECT_TRUE(ui.live());
  EXPECT_EQ(f.commands, 1);
}
TEST(AccountSurface, UnsupportedServiceKeepsOfflineExit) {
  Fake f;
  f.value.available = false;
  ServiceChoice ui(f);
  ui.enter();
  EXPECT_EQ(ui.selected(), 1);
  ui.chooseChoice(0);
  EXPECT_EQ(ui.selected(), 1);
  ui.chooseChoice(2);
  EXPECT_EQ(f.received.service, 0);
}
TEST(AccountSurface, LabelsAreBoundedAndCapabilityAbsenceIsExplicit) {
  Fake f;
  f.value.serviceLabel.fill('X');
  ServiceChoice ui(f);
  ui.enter();
  EXPECT_EQ(ui.view().serviceLabel.back(), 0);
  EXPECT_FALSE(ui.view().accountSupported);
  EXPECT_FALSE(ui.view().syncReady);
  EXPECT_FALSE(ui.view().catalogReady);
  for (int i = 0; i < 10; ++i) ui.stepChoice(1);
  EXPECT_EQ(ui.focus(), 3);
  ui.chooseChoice(99);
  EXPECT_TRUE(ui.live());
}
}  // namespace
