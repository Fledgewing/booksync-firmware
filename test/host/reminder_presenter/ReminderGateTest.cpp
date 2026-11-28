#include <gtest/gtest.h>

#include "booksync/interface/Library.h"
#include "booksync/services/RemindersFacade.h"
#include "booksync/storage/RemindersStore.h"
#include "booksync/system/ReadingGate.h"
using namespace booksync;
namespace {
class GateLibrary final : public Library {
 public:
  RemindersFacade& facade;
  explicit GateLibrary(RemindersFacade& f) : facade(f) {}
  Capabilities capabilities() const override { return {}; }
  Snapshot snapshot() const override { return {}; }
  CommandStatus submit(Intent) override { return CommandStatus::Unavailable; }
  ReminderAuthorization remindersAuthorize(ReminderClaim claim) override { return facade.authorize(claim); }
};
class ReminderGateTest : public ::testing::Test {
 protected:
  storage::RemindersStore store{{"/reminders", "/reminders.tmp", "/reminders.bak"}};
  Reminders service{store.port()};
  RemindersFacade facade{service};
  GateLibrary library{facade};
  ReminderNow now{1795813200, 0, 0, true, true, false, true, false};
  ReminderClaim claim{};
  void SetUp() override {
    card = {};
    facade.work(now);
    ReminderMutation mutation{};
    mutation.key = 1;
    mutation.fields.enabled = true;
    mutation.fields.minute = 1261;
    mutation.fields.days = 127;
    ASSERT_EQ(facade.mutate(mutation).status, ReminderStatus::Pending);
    facade.work(now);
    now.wall += 60;
    now.elapsed += 60000;
    facade.work(now);
    ReminderDue due{};
    ASSERT_TRUE(facade.due(due));
    ASSERT_EQ(facade.claim(due, 7).status, ReminderStatus::Pending);
    facade.work(now);
    const auto grant = facade.result();
    ASSERT_EQ(grant.status, ReminderStatus::Saved);
    claim = {due.source, due.kind, due.id, due.revision, grant.token, 7, due.session, due.sequence};
  }
  ReminderDrawContext context() const { return {{true, true, false}, 7, now.elapsed, now.known, true, false}; }
};
TEST_F(ReminderGateTest, GenericPresentationAndBooleanCannotExerciseException) {
  EXPECT_FALSE(ReadingGate::mayShow(Presentation::Reminder, {true, true, false}, true));
  EXPECT_FALSE(ReadingGate::mayShow(Presentation::Reminder, {false, true, false}, true));
}
TEST_F(ReminderGateTest, OnlyRealClaimGetsOneTypedDrawWithoutStorage) {
  card.events.clear();
  auto permit = ReadingGate::reminderPermit(library, claim, {true, true, false});
  EXPECT_TRUE(ReadingGate::drawReminder(permit, context()));
  EXPECT_FALSE(ReadingGate::drawReminder(permit, context()));
  EXPECT_TRUE(card.events.empty());
}
TEST_F(ReminderGateTest, ForgedSourceRevisionAndTokenNeverGrant) {
  auto forged = claim;
  forged.source = ReminderSource::Managed;
  auto first = ReadingGate::reminderPermit(library, forged, {true, true, false});
  EXPECT_FALSE(ReadingGate::drawReminder(first, context()));
  forged = claim;
  forged.revision++;
  auto second = ReadingGate::reminderPermit(library, forged, {true, true, false});
  EXPECT_FALSE(ReadingGate::drawReminder(second, context()));
  forged = claim;
  forged.token++;
  auto third = ReadingGate::reminderPermit(library, forged, {true, true, false});
  EXPECT_FALSE(ReadingGate::drawReminder(third, context()));
}
class GateRefusal : public ReminderGateTest, public ::testing::WithParamInterface<unsigned> {};
TEST_P(GateRefusal, CurrentStateRefusesWithoutARevivedGrant) {
  auto permit = ReadingGate::reminderPermit(library, claim, {true, true, false});
  auto c = context();
  if (GetParam() == 0) c.state.visible = false;
  if (GetParam() == 1) c.state.sleeping = true;
  if (GetParam() == 2) c.otherModal = true;
  EXPECT_FALSE(ReadingGate::drawReminder(permit, c));
  EXPECT_FALSE(ReadingGate::drawReminder(permit, context()));
}
INSTANTIATE_TEST_SUITE_P(HiddenSleepModal, GateRefusal, ::testing::Range(0u, 3u));
TEST_F(ReminderGateTest, ClockLossAndLifetimeRetirementInvalidateUndrawnPermit) {
  auto permit = ReadingGate::reminderPermit(library, claim, {true, true, false});
  auto c = context();
  c.clockKnown = false;
  EXPECT_FALSE(ReadingGate::drawReminder(permit, c));
  c = context();
  EXPECT_FALSE(ReadingGate::drawReminder(permit, c));
}
TEST_F(ReminderGateTest, OwnerAToBToACannotReviveAnOldPermit) {
  auto permit = ReadingGate::reminderPermit(library, claim, {true, true, false});
  auto c = context();
  c.lifetime = 8;
  EXPECT_FALSE(ReadingGate::drawReminder(permit, c));
  c.lifetime = 7;
  EXPECT_FALSE(ReadingGate::drawReminder(permit, c));
}
TEST_F(ReminderGateTest, ExpiredUndrawnGrantCannotShow) {
  auto permit = ReadingGate::reminderPermit(library, claim, {false, true, false});
  auto c = context();
  c.elapsed += 3600001;
  EXPECT_FALSE(ReadingGate::drawReminder(permit, c));
}
}  // namespace

namespace {
TEST_F(ReminderGateTest, PermitMoveTransfersItsSingleDraw) {
  auto original = ReadingGate::reminderPermit(library, claim, {true, true, false});
  auto moved = std::move(original);
  EXPECT_FALSE(ReadingGate::drawReminder(original, context()));
  EXPECT_TRUE(ReadingGate::drawReminder(moved, context()));
  EXPECT_FALSE(ReadingGate::drawReminder(moved, context()));
}
TEST_F(ReminderGateTest, TimePermitAllowsHomeThroughInclusiveExpiry) {
  auto permit = ReadingGate::reminderPermit(library, claim, {false, true, false});
  auto c = context();
  c.state.reading = false;
  c.page = false;
  c.elapsed += 3600000;
  EXPECT_TRUE(ReadingGate::drawReminder(permit, c));
}
TEST_F(ReminderGateTest, ServiceClockLossBeforeAuthorizationDenies) {
  now.known = false;
  facade.work(now);
  auto permit = ReadingGate::reminderPermit(library, claim, {true, true, false});
  EXPECT_FALSE(ReadingGate::drawReminder(permit, context()));
}
}  // namespace

TEST_F(ReminderGateTest, NativeSleepRequiresDrawAndFailureRestoresVisibleGrant) {
  facade.configureSleep({[](void*) { return false; }, nullptr});
  facade.sleepAvailable(true);
  EXPECT_EQ(service.respond(claim.token, 7, ReminderAction::Sleep, now).status, ReminderStatus::Stale);
  auto permit = ReadingGate::reminderPermit(library, claim, {true, true, false});
  ASSERT_TRUE(ReadingGate::drawReminder(permit, context()));
  EXPECT_EQ(facade.respond(claim.token, 7, ReminderAction::Sleep).status, ReminderStatus::Pending);
  facade.work(now);
  EXPECT_EQ(facade.result().status, ReminderStatus::Unavailable);
  EXPECT_EQ(facade.respond(claim.token, 7, ReminderAction::GotIt).status, ReminderStatus::Pending);
  facade.work(now);
  EXPECT_EQ(facade.result().status, ReminderStatus::Saved);
}
TEST_F(ReminderGateTest, NativeSleepCheckpointRunsOnceAfterExplicitResponse) {
  unsigned calls = 0;
  facade.configureSleep({[](void* context) {
                           ++*static_cast<unsigned*>(context);
                           return true;
                         },
                         &calls});
  facade.sleepAvailable(true);
  auto permit = ReadingGate::reminderPermit(library, claim, {true, true, false});
  ASSERT_TRUE(ReadingGate::drawReminder(permit, context()));
  EXPECT_EQ(calls, 0);
  facade.respond(claim.token, 7, ReminderAction::Sleep);
  facade.work(now);
  EXPECT_TRUE(facade.result().sleep);
  EXPECT_EQ(calls, 1);
  facade.respond(claim.token, 7, ReminderAction::Sleep);
  facade.work(now);
  EXPECT_EQ(facade.result().status, ReminderStatus::Stale);
  EXPECT_EQ(calls, 1);
}
TEST_F(ReminderGateTest, RetiredViewStillCommitsItsQueuedSnoozeWithoutPresentation) {
  facade.respond(claim.token, 7, ReminderAction::Snooze10);
  const auto before = facade.events().changed;
  facade.retire(7);
  facade.work(now);
  EXPECT_EQ(facade.result().status, ReminderStatus::Saved);
  EXPECT_EQ(facade.events().changed, before + 1);
  EXPECT_EQ(service.load(), ReminderStorage::Ready);
  now.wall += 600;
  now.elapsed += 600000;
  ReminderDue next;
  ASSERT_TRUE(service.next(now, next));
  EXPECT_EQ(next.sequence, 1);
}
TEST_F(ReminderGateTest, RetiredUnstartedSleepCannotAffectDevice) {
  unsigned calls = 0;
  facade.configureSleep({[](void* context) {
                           ++*static_cast<unsigned*>(context);
                           return true;
                         },
                         &calls});
  facade.sleepAvailable(true);
  auto permit = ReadingGate::reminderPermit(library, claim, {true, true, false});
  ASSERT_TRUE(ReadingGate::drawReminder(permit, context()));
  facade.respond(claim.token, 7, ReminderAction::Sleep);
  facade.retire(7);
  facade.work(now);
  EXPECT_EQ(calls, 0);
}
TEST_F(ReminderGateTest, ClaimTicketTimeoutDoesNotConsumeStillEligibleOccurrence) {
  facade.retire(7);
  // The already shown fixture was consumed. Create a fresh occurrence tomorrow.
  now.wall += 86400;
  now.elapsed += 86400000;
  facade.work(now, true);
  ReminderDue due;
  ASSERT_TRUE(facade.due(due));
  ASSERT_EQ(facade.claim(due, 8).status, ReminderStatus::Pending);
  now.wall += 120;
  now.elapsed += 120000;
  facade.work(now);
  EXPECT_EQ(facade.result().status, ReminderStatus::Unavailable);
  ASSERT_TRUE(facade.due(due));
  EXPECT_EQ(facade.claim(due, 9).status, ReminderStatus::Pending);
  facade.work(now);
  EXPECT_EQ(facade.result().status, ReminderStatus::Saved);
}
