#include <gtest/gtest.h>

#include "booksync/system/ReminderClaimRetry.h"
using namespace booksync;
TEST(ReminderRetry, FailedClaimWaitsOneMinuteAndDoesNotUseWallTime) {
  ReminderClaimRetry retry;
  ReminderDue due{};
  due.id = 2;
  due.revision = 1;
  due.day = 2;
  retry.failed(ReminderStatus::StorageError, due, 100);
  EXPECT_FALSE(retry.allows(due, 100));
  EXPECT_FALSE(retry.allows(due, 60099));
  EXPECT_TRUE(retry.allows(due, 60100));
  due.effective += 86400;
  EXPECT_FALSE(retry.allows(due, 200));
}
TEST(ReminderRetry, ANewVisibleLifetimeCanRetryWithoutRevivingTheOldTicket) {
  ReminderClaimRetry retry;
  ReminderDue due{};
  due.id = 2;
  retry.failed(ReminderStatus::Unavailable, due, 0);
  EXPECT_FALSE(retry.allows(due, 0));
  retry.reset();
  EXPECT_TRUE(retry.allows(due, 0));
}
TEST(ReminderRetry, StaleCandidateDoesNotDelayANewArbitrationResult) {
  ReminderClaimRetry retry;
  ReminderDue due{};
  due.id = 2;
  retry.failed(ReminderStatus::Stale, due, 0);
  EXPECT_TRUE(retry.allows(due, 0));
}
class ReminderRetryIdentity : public ::testing::TestWithParam<unsigned> {};
TEST_P(ReminderRetryIdentity, DifferentOccurrenceDoesNotInheritTheFailure) {
  ReminderClaimRetry retry;
  ReminderDue due{};
  due.id = 2;
  due.revision = 1;
  retry.failed(ReminderStatus::StorageError, due, 0);
  switch (GetParam()) {
    case 0:
      ++due.id;
      break;
    case 1:
      ++due.revision;
      break;
    case 2:
      ++due.day;
      break;
    case 3:
      ++due.sequence;
      break;
    case 4:
      ++due.session;
      break;
    case 5:
      due.kind = ReminderKind::Interval;
      break;
    case 6:
      due.source = ReminderSource::Managed;
      break;
  }
  EXPECT_TRUE(retry.allows(due, 0));
}
INSTANTIATE_TEST_SUITE_P(EventParts, ReminderRetryIdentity, ::testing::Range(0u, 7u));
