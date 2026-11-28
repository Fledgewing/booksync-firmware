#include <gtest/gtest.h>

#include <cstring>
#include <ctime>
#include <unordered_map>

#include "booksync/services/Reminders.h"
#include "booksync/services/RemindersFacade.h"
#include "booksync/storage/RemindersStore.h"
using namespace booksync;
namespace {
constexpr storage::PublishPaths paths{"/reminders", "/reminders.tmp", "/reminders.bak"};
class RemindersTest : public ::testing::Test {
 protected:
  storage::RemindersStore store{paths};
  Reminders service{store.port()};
  ReminderNow now{1795813200, 0, 0, true, true, false, true, false};  // Friday 21:00 UTC
  void SetUp() override {
    card = {};
    ASSERT_EQ(service.load(), ReminderStorage::Ready);
  }
  ReminderMutation time(uint16_t minute = 1261) {
    ReminderMutation request{};
    request.operation = ReminderOperation::Add;
    request.key = 1;
    request.fields.enabled = true;
    request.fields.minute = minute;
    request.fields.days = 127;
    return request;
  }
  ReminderResult add(uint16_t minute = 1261) { return service.mutate(time(minute), now); }
  void eyes(unsigned minutes = 20) {
    ReminderMutation m{};
    m.id = 1;
    m.revision = service.reminderData().intervalRevision;
    m.key = service.nextMutation();
    m.operation = ReminderOperation::Edit;
    m.fields.kind = ReminderKind::Interval;
    m.fields.intervalMinutes = minutes;
    m.fields.enabled = true;
    ASSERT_EQ(service.mutate(m, now).status, ReminderStatus::Saved);
    service.activity(now, true);
  }
  void readMinutes(unsigned minutes) {
    for (unsigned i = 0; i < minutes; ++i) {
      now.elapsed += 60000;
      now.wall += 60;
      service.activity(now, true);
    }
  }
};
TEST_F(RemindersTest, DefaultIntervalOffTwentyAndBounded) {
  EXPECT_FALSE(service.intervalEnabled());
  EXPECT_EQ(service.intervalMinutes(), 20);
  EXPECT_LE(sizeof(Reminders), 4096);
  EXPECT_LE(sizeof(ReminderDue), 1024);
}
TEST_F(RemindersTest, EightRowsAndIdempotentAdd) {
  auto request = time();
  auto result = service.mutate(request, now);
  EXPECT_EQ(result.status, ReminderStatus::Saved);
  EXPECT_EQ(service.mutate(request, now).id, result.id);
  for (unsigned i = 2; i <= 8; ++i) {
    request.key = i;
    EXPECT_EQ(service.mutate(request, now).status, ReminderStatus::Saved);
  }
  request.key = 9;
  EXPECT_EQ(service.mutate(request, now).status, ReminderStatus::Limit);
}
TEST_F(RemindersTest, SaveNeverBackfiresAndMissedWindowInclusive) {
  ASSERT_EQ(add(1260).status, ReminderStatus::Saved);
  ReminderDue due{};
  EXPECT_FALSE(service.next(now, due));
  now.wall += 86400 + 3600;
  EXPECT_TRUE(service.next(now, due));
  EXPECT_TRUE(due.missed);
  now.wall++;
  EXPECT_FALSE(service.next(now, due));
}
TEST_F(RemindersTest, UnknownClockNeverGrantsBedtime) {
  ASSERT_EQ(add().status, ReminderStatus::Saved);
  now.wall += 60;
  now.known = false;
  ReminderDue due{};
  EXPECT_FALSE(service.next(now, due));
  now.known = true;
  EXPECT_TRUE(service.next(now, due));
}
TEST_F(RemindersTest, ClaimsSurviveRestartAndBackwardOffsetChanges) {
  ASSERT_EQ(add().status, ReminderStatus::Saved);
  now.wall += 60;
  ReminderDue due{};
  ASSERT_TRUE(service.next(now, due));
  ASSERT_EQ(service.claim(due, 7, now).status, ReminderStatus::Saved);
  Reminders restarted{store.port()};
  ASSERT_EQ(restarted.load(), ReminderStorage::Ready);
  EXPECT_FALSE(restarted.next(now, due));
  now.offsetMinutes = -60;
  now.wall += 3600;
  EXPECT_FALSE(restarted.next(now, due));
}
TEST_F(RemindersTest, SnoozeUsesElapsedTimeWithinBoot) {
  ASSERT_EQ(add().status, ReminderStatus::Saved);
  now.wall += 60;
  ReminderDue due{};
  ASSERT_TRUE(service.next(now, due));
  auto grant = service.claim(due, 7, now);
  ASSERT_EQ(grant.status, ReminderStatus::Saved);
  ASSERT_EQ(service.respond(grant.token, 7, ReminderAction::Snooze10, now).status, ReminderStatus::Saved);
  now.wall += 7200;
  now.elapsed = 599999;
  EXPECT_FALSE(service.next(now, due));
  now.elapsed = 600000;
  service.activity(now, true);
  EXPECT_TRUE(service.next(now, due));
}
TEST_F(RemindersTest, CorruptStoreIsRetainedAndCannotBeOverwritten) {
  card.files[paths.current] = "damaged";
  card.disk = card.files;
  EXPECT_EQ(service.load(), ReminderStorage::Unreadable);
  EXPECT_EQ(add().status, ReminderStatus::StorageError);
  EXPECT_EQ(card.files[paths.current], "damaged");
}
TEST_F(RemindersTest, FailedSaveRetainsSchedule) {
  auto first = add();
  ASSERT_EQ(first.status, ReminderStatus::Saved);
  card.full = true;
  auto request = time();
  request.key = 2;
  EXPECT_EQ(service.mutate(request, now).status, ReminderStatus::StorageError);
  EXPECT_EQ(service.count(), 1);
}
}  // namespace
namespace {
TEST_F(RemindersTest, ValidationRejectsKindsAndMessages) {
  auto request = time();
  request.fields.days = 0;
  EXPECT_EQ(service.mutate(request, now).status, ReminderStatus::Invalid);
  request = time();
  request.fields.minute = 1440;
  EXPECT_EQ(service.mutate(request, now).status, ReminderStatus::Invalid);
  request = time();
  std::memset(request.fields.message, 'x', 61);
  EXPECT_EQ(service.mutate(request, now).status, ReminderStatus::Invalid);
  request = time();
  std::strcpy(request.fields.message, "bad\nmessage");
  EXPECT_EQ(service.mutate(request, now).status, ReminderStatus::Invalid);
  request = time();
  request.fields.message[0] = char(0xc0);
  EXPECT_EQ(service.mutate(request, now).status, ReminderStatus::Invalid);
  request = time();
  request.fields.kind = ReminderKind::Interval;
  request.fields.intervalMinutes = 20;
  EXPECT_EQ(service.mutate(request, now).status, ReminderStatus::Invalid);
}
TEST_F(RemindersTest, BackupRecoveryIgnoresTmpAndPreservesDamage) {
  ASSERT_EQ(add().status, ReminderStatus::Saved);
  auto request = time(1262);
  request.key = 2;
  ASSERT_EQ(service.mutate(request, now).status, ReminderStatus::Saved);
  card.files[paths.current] = "corrupt";
  card.files[paths.staging] = "uncommitted";
  EXPECT_EQ(service.load(), ReminderStorage::Recovered);
  EXPECT_EQ(service.count(), 1);
  EXPECT_EQ(card.files[paths.current], "corrupt");
  EXPECT_EQ(card.files[paths.staging], "uncommitted");
}
TEST_F(RemindersTest, PublicationPowerCutsSelectOnlyCompleteSchedules) {
  ASSERT_EQ(add().status, ReminderStatus::Saved);
  const auto initial = card;
  auto request = time(1262);
  request.key = 2;
  card.events.clear();
  ASSERT_EQ(service.mutate(request, now).status, ReminderStatus::Saved);
  const auto events = card.events.size();
  for (size_t cut = 0; cut < events; ++cut) {
    card = initial;
    ASSERT_EQ(service.load(), ReminderStorage::Ready);
    card.events.clear();
    card.stop = static_cast<int>(cut);
    try {
      service.mutate(request, now);
    } catch (const PowerCut&) {
    }
    card.restart();
    const auto state = service.load();
    EXPECT_NE(state, ReminderStorage::Unreadable) << cut;
    EXPECT_GE(service.count(), 1u);
    EXPECT_LE(service.count(), 2u);
    EXPECT_EQ(card.handles, 0) << cut;
  }
}
TEST_F(RemindersTest, NoCascadeUntilFreshInput) {
  ASSERT_EQ(add().status, ReminderStatus::Saved);
  auto request = time();
  request.key = 2;
  ASSERT_EQ(service.mutate(request, now).status, ReminderStatus::Saved);
  now.wall += 60;
  ReminderDue due{};
  ASSERT_TRUE(service.next(now, due));
  auto grant = service.claim(due, 7, now);
  ASSERT_EQ(grant.status, ReminderStatus::Saved);
  ASSERT_EQ(service.respond(grant.token, 7, ReminderAction::GotIt, now).status, ReminderStatus::Saved);
  EXPECT_FALSE(service.next(now, due));
  service.activity(now, true);
  EXPECT_TRUE(service.next(now, due));
}
TEST_F(RemindersTest, StaleAndHiddenClaimsNeverWrite) {
  ASSERT_EQ(add().status, ReminderStatus::Saved);
  now.wall += 60;
  ReminderDue due{};
  ASSERT_TRUE(service.next(now, due));
  card.events.clear();
  now.visible = false;
  EXPECT_EQ(service.claim(due, 7, now).status, ReminderStatus::Stale);
  EXPECT_TRUE(card.events.empty());
  now.visible = true;
  now.sleeping = true;
  EXPECT_EQ(service.claim(due, 7, now).status, ReminderStatus::Stale);
}
TEST_F(RemindersTest, IntervalUsesReadingTimeAndNeverWritesOnTicksOrDone) {
  ReminderMutation m{};
  m.id = 1;
  m.revision = 1;
  m.key = 1;
  m.operation = ReminderOperation::Edit;
  m.fields.kind = ReminderKind::Interval;
  m.fields.intervalMinutes = 20;
  m.fields.enabled = true;
  ASSERT_EQ(service.mutate(m, now).status, ReminderStatus::Saved);
  card.events.clear();
  now.known = false;
  service.activity(now, true);
  for (unsigned i = 1; i <= 20; ++i) {
    now.elapsed = i * 60000;
    service.activity(now, true);
  }
  ReminderDue due{};
  ASSERT_TRUE(service.next(now, due));
  EXPECT_EQ(due.kind, ReminderKind::Interval);
  auto grant = service.claim(due, 7, now);
  ASSERT_EQ(grant.status, ReminderStatus::Saved);
  EXPECT_EQ(service.respond(grant.token, 7, ReminderAction::Done, now).status, ReminderStatus::Saved);
  EXPECT_TRUE(card.events.empty());
}
TEST_F(RemindersTest, FiveMinuteIdleResetsBeforeResumeInput) {
  ReminderMutation m{};
  m.id = 1;
  m.revision = 1;
  m.key = 1;
  m.operation = ReminderOperation::Edit;
  m.fields.kind = ReminderKind::Interval;
  m.fields.intervalMinutes = 20;
  m.fields.enabled = true;
  ASSERT_EQ(service.mutate(m, now).status, ReminderStatus::Saved);
  service.activity(now, true);
  for (unsigned i = 1; i <= 19; ++i) {
    now.elapsed = i * 60000;
    service.activity(now, true);
  }
  now.elapsed += 300000;
  service.activity(now, true);
  ReminderDue due{};
  EXPECT_FALSE(service.next(now, due));
  now.elapsed += 60000;
  service.activity(now, true);
  EXPECT_FALSE(service.next(now, due));
}
}  // namespace

namespace {
TEST_F(RemindersTest, FacadeEnqueuesWithoutIoAndPublishesEventsOutsidePaint) {
  RemindersFacade facade{service};
  facade.work(now);
  card.events.clear();
  ASSERT_EQ(facade.mutate(time()).status, ReminderStatus::Pending);
  EXPECT_TRUE(card.events.empty());
  facade.work(now);
  EXPECT_EQ(facade.result().status, ReminderStatus::Saved);
  EXPECT_EQ(facade.events().changed, 1);
  card.events.clear();
  ReminderList list{};
  facade.list(list);
  EXPECT_EQ(list.count, 2);
  EXPECT_TRUE(card.events.empty());
  now.wall += 60;
  facade.work(now);
  EXPECT_EQ(facade.events().due, 1);
  EXPECT_TRUE(card.events.empty());
  ReminderDue due{};
  ASSERT_TRUE(facade.due(due));
  ASSERT_EQ(facade.claim(due, 7).status, ReminderStatus::Pending);
  EXPECT_TRUE(card.events.empty());
  facade.retire(7);
  facade.work(now);
  EXPECT_TRUE(card.events.empty());
}
TEST_F(RemindersTest, PublicationIoFailuresNeverAcknowledgeBeforeDurability) {
  ASSERT_EQ(add().status, ReminderStatus::Saved);
  const auto initial = card;
  auto request = time(1262);
  request.key = 2;
  card.events.clear();
  ASSERT_EQ(service.mutate(request, now).status, ReminderStatus::Saved);
  const auto events = card.events.size();
  for (size_t failure = 0; failure < events; ++failure) {
    card = initial;
    ASSERT_EQ(service.load(), ReminderStorage::Ready);
    card.events.clear();
    card.fail = static_cast<int>(failure);
    const auto result = service.mutate(request, now);
    if (result.status == ReminderStatus::Saved)
      EXPECT_EQ(card.disk[paths.current], card.files[paths.current]);
    else
      EXPECT_EQ(service.count(), 1u);
    EXPECT_EQ(card.handles, 0) << failure;
  }
}
}  // namespace
namespace {
TEST_F(RemindersTest, UnknownClockSaveNeverBackfiresBeforeItsElapsedSaveTime) {
  now.known = false;
  now.wall -= 3600;
  ASSERT_EQ(add(1258).status, ReminderStatus::Saved);
  now.wall += 3600;
  now.known = true;
  now.elapsed = 60000;
  ReminderDue due{};
  EXPECT_FALSE(service.next(now, due));
}
TEST_F(RemindersTest, UnknownClockSaveCanFireAFutureOccurrenceAfterRecovery) {
  now.known = false;
  ASSERT_EQ(add().status, ReminderStatus::Saved);
  now.wall += 90;
  now.elapsed = 90000;
  now.known = true;
  ReminderDue due{};
  EXPECT_TRUE(service.next(now, due));
}
}  // namespace
namespace {
TEST_F(RemindersTest, RetriedAddAfterDeleteCannotCreateAnotherRow) {
  auto original = time();
  auto saved = service.mutate(original, now);
  ASSERT_EQ(saved.status, ReminderStatus::Saved);
  ReminderMutation remove{};
  remove.id = saved.id;
  remove.revision = saved.revision;
  remove.key = service.nextMutation();
  remove.operation = ReminderOperation::Delete;
  ASSERT_EQ(service.mutate(remove, now).status, ReminderStatus::Saved);
  EXPECT_EQ(service.mutate(original, now).status, ReminderStatus::Stale);
  EXPECT_EQ(service.count(), 0);
}
}  // namespace

namespace {
class IntervalChoices : public RemindersTest, public ::testing::WithParamInterface<unsigned> {};
TEST_P(IntervalChoices, ExactIntervalAndWallClockIndependence) {
  eyes(GetParam());
  now.known = false;
  readMinutes(GetParam() - 1);
  ReminderDue due{};
  EXPECT_FALSE(service.next(now, due));
  now.wall -= 86400;
  readMinutes(1);
  ASSERT_TRUE(service.next(now, due));
  EXPECT_EQ(due.kind, ReminderKind::Interval);
}
INSTANTIATE_TEST_SUITE_P(FixedIntervals, IntervalChoices, ::testing::Values(20u, 30u, 45u, 60u));
class PauseBoundary : public RemindersTest, public ::testing::WithParamInterface<uint64_t> {};
TEST_P(PauseBoundary, OneGapAcrossMenuAndResumeInput) {
  eyes();
  readMinutes(19);
  now.page = false;
  service.activity(now);
  now.elapsed += GetParam();
  now.page = true;
  service.activity(now, true);
  readMinutes(1);
  ReminderDue due{};
  EXPECT_EQ(service.next(now, due), GetParam() < 300000);
}
INSTANTIATE_TEST_SUITE_P(IdleBoundary, PauseBoundary, ::testing::Values(299999ULL, 300000ULL));
TEST_F(RemindersTest, TimeUnderEyeSheetDoesNotCountTowardNextInterval) {
  eyes();
  readMinutes(20);
  ReminderDue due{};
  ASSERT_TRUE(service.next(now, due));
  auto grant = service.claim(due, 7, now);
  ASSERT_EQ(grant.status, ReminderStatus::Saved);
  now.elapsed += 120000;
  now.wall += 120;
  ASSERT_EQ(service.respond(grant.token, 7, ReminderAction::Done, now).status, ReminderStatus::Saved);
  service.activity(now, true);
  readMinutes(19);
  EXPECT_FALSE(service.next(now, due));
  readMinutes(1);
  EXPECT_TRUE(service.next(now, due));
}
TEST_F(RemindersTest, BedtimeWinsAndConsumesSimultaneousEyeThreshold) {
  ASSERT_EQ(add(1280).status, ReminderStatus::Saved);
  eyes();
  readMinutes(20);
  ReminderDue due{};
  ASSERT_TRUE(service.next(now, due));
  EXPECT_EQ(due.kind, ReminderKind::Time);
  auto grant = service.claim(due, 7, now);
  ASSERT_EQ(grant.status, ReminderStatus::Saved);
  ASSERT_EQ(service.respond(grant.token, 7, ReminderAction::GotIt, now).status, ReminderStatus::Saved);
  service.activity(now, true);
  EXPECT_FALSE(service.next(now, due));
  readMinutes(19);
  EXPECT_FALSE(service.next(now, due));
  readMinutes(1);
  EXPECT_TRUE(service.next(now, due));
}
TEST_F(RemindersTest, NavigationRetiresUndrawnEyesAndRestartsFullInterval) {
  eyes();
  readMinutes(20);
  ReminderDue due{};
  ASSERT_TRUE(service.next(now, due));
  const auto old = due;
  now.modal = true;
  now.page = false;
  service.activity(now);
  EXPECT_EQ(service.claim(old, 7, now).status, ReminderStatus::Stale);
  now.modal = false;
  now.page = true;
  service.activity(now, true);
  EXPECT_FALSE(service.next(now, due));
  readMinutes(20);
  EXPECT_TRUE(service.next(now, due));
  EXPECT_NE(due.sequence, old.sequence);
}
TEST_F(RemindersTest, IntervalRestartDiscardsElapsedHistoryAndRejectsAddDelete) {
  eyes();
  readMinutes(19);
  ASSERT_EQ(service.load(), ReminderStorage::Ready);
  service.activity(now, true);
  readMinutes(1);
  ReminderDue due{};
  EXPECT_FALSE(service.next(now, due));
  ReminderMutation m{};
  m.key = service.nextMutation();
  m.id = 1;
  m.operation = ReminderOperation::Delete;
  EXPECT_EQ(service.mutate(m, now).status, ReminderStatus::Invalid);
  m.id = 0;
  m.operation = ReminderOperation::Add;
  m.fields.kind = ReminderKind::Interval;
  m.fields.intervalMinutes = 20;
  EXPECT_EQ(service.mutate(m, now).status, ReminderStatus::Invalid);
}
TEST_F(RemindersTest, ExplicitTurnOffFailureKeepsEnabledAndDoneAvailable) {
  eyes();
  readMinutes(20);
  ReminderDue due{};
  ASSERT_TRUE(service.next(now, due));
  auto grant = service.claim(due, 7, now);
  ASSERT_EQ(grant.status, ReminderStatus::Saved);
  card.full = true;
  EXPECT_EQ(service.respond(grant.token, 7, ReminderAction::TurnOff, now).status, ReminderStatus::StorageError);
  EXPECT_TRUE(service.intervalEnabled());
  EXPECT_EQ(service.respond(grant.token, 7, ReminderAction::Done, now).status, ReminderStatus::Saved);
}
TEST_F(RemindersTest, WrongKindResponsesAndForgedSourceCannotExerciseClaims) {
  ASSERT_EQ(add().status, ReminderStatus::Saved);
  now.wall += 60;
  ReminderDue due{};
  ASSERT_TRUE(service.next(now, due));
  auto forged = due;
  forged.source = ReminderSource::Managed;
  EXPECT_EQ(service.claim(forged, 7, now).status, ReminderStatus::Stale);
  auto grant = service.claim(due, 7, now);
  ASSERT_EQ(grant.status, ReminderStatus::Saved);
  EXPECT_EQ(service.respond(grant.token, 7, ReminderAction::TurnOff, now).status, ReminderStatus::Invalid);
  EXPECT_EQ(service.respond(grant.token, 8, ReminderAction::Sleep, now).status, ReminderStatus::Stale);
  service.retire(7);
  EXPECT_EQ(service.respond(grant.token, 7, ReminderAction::Sleep, now).status, ReminderStatus::Stale);
}
TEST_F(RemindersTest, UnknownClockSnoozeSurvivesRestartWithoutDrawing) {
  ASSERT_EQ(add().status, ReminderStatus::Saved);
  now.wall += 60;
  ReminderDue due{};
  ASSERT_TRUE(service.next(now, due));
  auto grant = service.claim(due, 7, now);
  ASSERT_EQ(service.respond(grant.token, 7, ReminderAction::Snooze10, now).status, ReminderStatus::Saved);
  ASSERT_EQ(service.load(), ReminderStorage::Ready);
  now.wall += 600;
  now.known = false;
  EXPECT_FALSE(service.next(now, due));
  now.known = true;
  EXPECT_TRUE(service.next(now, due));
  now.wall += 3601;
  EXPECT_FALSE(service.next(now, due));
}
TEST_F(RemindersTest, RevisionInvalidatesPendingSnoozeAndOldGrant) {
  const auto saved = add();
  ASSERT_EQ(saved.status, ReminderStatus::Saved);
  now.wall += 60;
  ReminderDue due{};
  ASSERT_TRUE(service.next(now, due));
  auto grant = service.claim(due, 7, now);
  ASSERT_EQ(service.respond(grant.token, 7, ReminderAction::Snooze10, now).status, ReminderStatus::Saved);
  auto edit = time(1300);
  edit.operation = ReminderOperation::Edit;
  edit.id = saved.id;
  edit.revision = saved.revision;
  edit.key = service.nextMutation();
  ASSERT_EQ(service.mutate(edit, now).status, ReminderStatus::Saved);
  now.wall += 600;
  service.activity(now, true);
  EXPECT_FALSE(service.next(now, due));
  EXPECT_EQ(service.respond(grant.token, 7, ReminderAction::Sleep, now).status, ReminderStatus::Stale);
}
}  // namespace
namespace {
TEST_F(RemindersTest, MissingStorageRefusesSaveAndNeverLoadsEmptySuccess) {
  ASSERT_EQ(add().status, ReminderStatus::Saved);
  card.present = false;
  auto request = time();
  request.key = service.nextMutation();
  EXPECT_EQ(service.mutate(request, now).status, ReminderStatus::StorageError);
  EXPECT_EQ(service.count(), 1);
  EXPECT_EQ(service.load(), ReminderStorage::Unreadable);
}
TEST_F(RemindersTest, ExplicitRetryIsQueuedWithoutIoAndRecoversAfterStorageReturns) {
  card.present = false;
  RemindersFacade facade{service};
  facade.work(now);
  card.events.clear();
  card.present = true;
  EXPECT_TRUE(facade.retry());
  EXPECT_TRUE(card.events.empty());
  facade.work(now);
  ReminderList list{};
  facade.list(list);
  EXPECT_EQ(list.storage, ReminderStorage::Ready);
}
TEST_F(RemindersTest, SetEnabledAcceptsOnlyItsBooleanAndStartsAfterSave) {
  const auto saved = add();
  ASSERT_EQ(saved.status, ReminderStatus::Saved);
  ReminderMutation m{};
  m.operation = ReminderOperation::SetEnabled;
  m.id = saved.id;
  m.revision = saved.revision;
  m.key = service.nextMutation();
  ASSERT_EQ(service.mutate(m, now).status, ReminderStatus::Saved);
  now.wall += 120;
  m.revision++;
  m.key = service.nextMutation();
  m.fields.enabled = true;
  ASSERT_EQ(service.mutate(m, now).status, ReminderStatus::Saved);
  ReminderDue due{};
  EXPECT_FALSE(service.next(now, due));
}
}  // namespace

namespace {
int64_t utc(unsigned year, unsigned month, unsigned day, unsigned hour = 0, unsigned minute = 0) {
  std::tm value{};
  value.tm_year = year - 1900;
  value.tm_mon = month - 1;
  value.tm_mday = day;
  value.tm_hour = hour;
  value.tm_min = minute;
  return timegm(&value);
}
class SelectedDay : public RemindersTest, public ::testing::WithParamInterface<unsigned> {};
TEST_P(SelectedDay, OnlySelectedLocalWeekdayIsEligible) {
  auto request = time();
  request.fields.days = 1u << GetParam();
  ASSERT_EQ(service.mutate(request, now).status, ReminderStatus::Saved);
  const unsigned delta = (GetParam() + 7 - 4) % 7;  // fixture starts Friday
  now.wall += int64_t(delta) * 86400 + 60;
  ReminderDue due{};
  EXPECT_TRUE(service.next(now, due));
  now.wall += 86400;
  EXPECT_FALSE(service.next(now, due));
}
INSTANTIATE_TEST_SUITE_P(SevenDays, SelectedDay, ::testing::Range(0u, 7u));
TEST_F(RemindersTest, MidnightChecksPreviousLocalDate) {
  ASSERT_EQ(add(1435).status, ReminderStatus::Saved);
  now.wall += 3 * 3600 + 5 * 60;
  ReminderDue due{};
  ASSERT_TRUE(service.next(now, due));
  EXPECT_TRUE(due.missed);
  EXPECT_EQ(now.wall - due.effective, 600);
}
TEST_F(RemindersTest, LeapDayAndFollowingMonthRemainIndependent) {
  now.wall = utc(2028, 2, 28, 23, 59);
  ASSERT_EQ(add(0).status, ReminderStatus::Saved);
  now.wall += 60;
  ReminderDue due{};
  ASSERT_TRUE(service.next(now, due));
  EXPECT_EQ(due.day, utc(2028, 2, 29) / 86400);
  ASSERT_EQ(service.claim(due, 7, now).status, ReminderStatus::Saved);
  service.retire(7);
  now.wall = utc(2028, 3, 1);
  ASSERT_TRUE(service.next(now, due));
  EXPECT_EQ(due.day, utc(2028, 3, 1) / 86400);
}
TEST_F(RemindersTest, OffsetGapAndRepeatHonorHighWater) {
  ASSERT_EQ(add(1350).status, ReminderStatus::Saved);
  now.wall += 45 * 60;
  now.offsetMinutes = 60;
  ReminderDue due{};
  ASSERT_TRUE(service.next(now, due));
  EXPECT_TRUE(due.missed);
  ASSERT_EQ(service.claim(due, 7, now).status, ReminderStatus::Saved);
  service.retire(7);
  now.offsetMinutes = -60;
  now.wall += 105 * 60;
  EXPECT_FALSE(service.next(now, due));
}
TEST_F(RemindersTest, LargeForwardJumpOffersOnlyMostRecentEligibleDate) {
  ASSERT_EQ(add().status, ReminderStatus::Saved);
  now.wall += 3 * 86400 + 60;
  ReminderDue due{};
  ASSERT_TRUE(service.next(now, due));
  EXPECT_EQ(due.day, now.wall / 86400);
  ASSERT_EQ(service.claim(due, 7, now).status, ReminderStatus::Saved);
  service.retire(7);
  EXPECT_FALSE(service.next(now, due));
}
TEST_F(RemindersTest, SnoozeCrossesMidnightWithoutRetestingWeekday) {
  now.wall = utc(2026, 11, 27, 23, 58);
  auto request = time(1439);
  request.fields.days = 1u << 4;
  ASSERT_EQ(service.mutate(request, now).status, ReminderStatus::Saved);
  now.wall += 60;
  ReminderDue due{};
  ASSERT_TRUE(service.next(now, due));
  const auto original = due;
  auto grant = service.claim(due, 7, now);
  ASSERT_EQ(grant.status, ReminderStatus::Saved);
  ASSERT_EQ(service.respond(grant.token, 7, ReminderAction::Snooze10, now).status, ReminderStatus::Saved);
  now.wall += 600;
  now.elapsed += 600000;
  now.offsetMinutes = 60;
  service.activity(now, true);
  ASSERT_TRUE(service.next(now, due));
  EXPECT_EQ(due.day, original.day);
  EXPECT_EQ(due.scheduled, original.scheduled);
}
TEST_F(RemindersTest, NewRegularClaimDoesNotChangeOlderPendingSnoozeOrigin) {
  ASSERT_EQ(add().status, ReminderStatus::Saved);
  now.wall += 60;
  ReminderDue due{};
  ASSERT_TRUE(service.next(now, due));
  auto grant = service.claim(due, 7, now);
  ASSERT_EQ(grant.status, ReminderStatus::Saved);
  ASSERT_EQ(service.respond(grant.token, 7, ReminderAction::Snooze10, now).status, ReminderStatus::Saved);
  // Inject the state of repeated, explicit snoozes extending past tomorrow's
  // regular due time. No synthetic backlog is allocated in production.
  ReminderData stored = service.reminderData();
  ReminderRecord* row = nullptr;
  for (auto& candidate : stored.rows)
    if (candidate.id == due.id) row = &candidate;
  ASSERT_NE(row, nullptr);
  const auto origin = row->snoozeDay;
  row->snooze += 86400;
  ASSERT_TRUE(store.save(stored));
  ASSERT_EQ(service.load(), ReminderStorage::Ready);
  now.wall += 86400;
  service.activity(now, true);
  ASSERT_TRUE(service.next(now, due));
  EXPECT_EQ(due.sequence, 0u);
  ASSERT_EQ(service.claim(due, 8, now).status, ReminderStatus::Saved);
  service.retire(8);
  now.wall += 600;
  ASSERT_TRUE(service.next(now, due));
  EXPECT_EQ(due.day, origin);
  EXPECT_NE(due.sequence, 0u);
}
}  // namespace

namespace {
TEST_F(RemindersTest, RepeatedIdleTicksResetTheSessionOnlyOnce) {
  eyes();
  readMinutes(20);
  ReminderDue first{};
  ASSERT_TRUE(service.next(now, first));
  for (unsigned i = 0; i < 100; ++i) {
    now.elapsed += 300000;
    service.activity(now);
  }
  service.activity(now, true);
  readMinutes(20);
  ReminderDue second{};
  ASSERT_TRUE(service.next(now, second));
  EXPECT_EQ(second.session, first.session + 1);
}
TEST_F(RemindersTest, SixtyFourByteCharactersUseMaximumMessageBudget) {
  auto request = time();
  const char glyph[] = "\xf0\x9f\x93\x96";
  for (unsigned i = 0; i < 60; ++i) std::memcpy(request.fields.message + i * 4, glyph, 4);
  EXPECT_EQ(service.mutate(request, now).status, ReminderStatus::Saved);
  for (const auto& row : service.reminderData().rows)
    if (row.id) EXPECT_EQ(std::strlen(row.fields.message), 240u);
}
}  // namespace

namespace {
TEST_F(RemindersTest, EverySnoozeFieldIsValidatedBeforePublication) {
  ASSERT_EQ(add().status, ReminderStatus::Saved);
  ReminderData changed = service.reminderData();
  ReminderRecord* row = nullptr;
  for (auto& candidate : changed.rows)
    if (candidate.id) row = &candidate;
  ASSERT_NE(row, nullptr);
  row->snoozeDay = 1;
  EXPECT_FALSE(Reminders::valid(changed));
  row->snoozeDay = -1;
  row->snoozeScheduled = 1;
  EXPECT_FALSE(Reminders::valid(changed));
  row->snooze = now.wall + 600;
  row->snoozeDay = static_cast<int32_t>(now.wall / 86400);
  row->snoozeScheduled = now.wall;
  EXPECT_FALSE(Reminders::valid(changed));  // Missing response sequence/high-water.
  row->sequence = 1;
  row->highWater = row->snoozeDay;
  ASSERT_TRUE(Reminders::valid(changed));
  row->snoozeDay = INT32_MAX;
  EXPECT_FALSE(Reminders::valid(changed));
  row->snoozeDay = row->highWater;
  row->snoozeScheduled = INT64_MAX;
  EXPECT_FALSE(Reminders::valid(changed));
}
TEST_F(RemindersTest, LosingClockAfterVisibleClaimCannotPersistAnInventedSnoozeWallTarget) {
  ASSERT_EQ(add().status, ReminderStatus::Saved);
  now.wall += 60;
  ReminderDue due;
  ASSERT_TRUE(service.next(now, due));
  const auto grant = service.claim(due, 7, now);
  ASSERT_EQ(grant.status, ReminderStatus::Saved);
  now.known = false;
  now.wall = 0;
  EXPECT_EQ(service.respond(grant.token, 7, ReminderAction::Snooze10, now).status, ReminderStatus::Unavailable);
  EXPECT_EQ(service.respond(grant.token, 7, ReminderAction::GotIt, now).status, ReminderStatus::Saved);
}
}  // namespace

namespace {
TEST_F(RemindersTest, AmbiguousFailureCannotAcknowledgeAnotherValidPayloadWithSameChecksum) {
  ASSERT_EQ(add().status, ReminderStatus::Saved);
  auto desired = service.reminderData();
  ReminderRecord* row = nullptr;
  for (auto& candidate : desired.rows)
    if (candidate.id) row = &candidate;
  ASSERT_NE(row, nullptr);
  const auto& original = card.files[paths.current];
  uint32_t prefix = 2166136261u;
  for (size_t i = 16; i < original.size() - 241; ++i) prefix = (prefix ^ static_cast<uint8_t>(original[i])) * 16777619u;
  // Host-only deterministic birthday search. Product storage must compare exact
  // intended bytes rather than treating this non-cryptographic checksum as ID.
  std::unordered_map<uint32_t, uint32_t> seen;
  seen.reserve(200000);
  uint32_t first = 0, second = 0;
  bool found = false;
  for (uint32_t i = 0; i < 1000000 && !found; ++i) {
    char text[9]{};
    std::snprintf(text, sizeof(text), "%08x", unsigned(i));
    uint32_t hash = prefix;
    for (unsigned j = 0; j < 8; ++j) hash = (hash ^ static_cast<uint8_t>(text[j])) * 16777619u;
    const auto inserted = seen.emplace(hash, i);
    if (!inserted.second) {
      first = inserted.first->second;
      second = i;
      found = true;
    }
  }
  ASSERT_TRUE(found);
  std::snprintf(row->fields.message, sizeof(row->fields.message), "%08x", unsigned(first));
  ASSERT_TRUE(store.save(desired));
  const auto before = card;
  const auto oldBytes = card.files[paths.current];
  std::snprintf(row->fields.message, sizeof(row->fields.message), "%08x", unsigned(second));
  card.events.clear();
  ASSERT_TRUE(store.save(desired));
  EXPECT_EQ(oldBytes.substr(12, 4), card.files[paths.current].substr(12, 4));
  ASSERT_NE(oldBytes, card.files[paths.current]);
  const auto fault = std::find(card.events.begin(), card.events.end(), "file-sync:before") - card.events.begin();
  ASSERT_LT(static_cast<size_t>(fault), card.events.size());
  card = before;
  card.events.clear();
  card.fail = static_cast<int>(fault);
  EXPECT_FALSE(store.save(desired));
  EXPECT_EQ(card.files[paths.current], oldBytes);
}
}  // namespace

namespace {
TEST(ReminderCapabilities, TaggedKindsExposeOnlyBoundedLocalOperations) {
  ReminderCapabilities unavailable;
  EXPECT_FALSE(unavailable);
  EXPECT_FALSE(unavailable.sleep);
  EXPECT_FALSE(unavailable.clockKnown);
  ReminderCapabilities supported{true};
  EXPECT_TRUE(supported);
  EXPECT_TRUE(supported.time);
  EXPECT_TRUE(supported.interval);
  EXPECT_TRUE(supported.localEdit);
  EXPECT_EQ(supported.timeLimit, 8);
  EXPECT_EQ(supported.messageCharacters, 60);
  EXPECT_EQ(supported.messageBytes, 240);
  EXPECT_EQ(supported.intervals[0], 20);
  EXPECT_EQ(supported.intervals[1], 30);
  EXPECT_EQ(supported.intervals[2], 45);
  EXPECT_EQ(supported.intervals[3], 60);
}
}  // namespace

namespace {
TEST_F(RemindersTest, EventsTagTheChangedKindWithoutPaintWork) {
  RemindersFacade facade{service};
  facade.work(now);
  ReminderMutation m{};
  m.id = 1;
  m.revision = 1;
  m.key = 1;
  m.operation = ReminderOperation::Edit;
  m.fields.kind = ReminderKind::Interval;
  m.fields.enabled = true;
  m.fields.intervalMinutes = 20;
  ASSERT_EQ(facade.mutate(m).status, ReminderStatus::Pending);
  facade.work(now);
  card.events.clear();
  const auto event = facade.events();
  EXPECT_NE(event.changed, 0);
  EXPECT_EQ(event.changedKind, ReminderKind::Interval);
  EXPECT_TRUE(card.events.empty());
}
}  // namespace
