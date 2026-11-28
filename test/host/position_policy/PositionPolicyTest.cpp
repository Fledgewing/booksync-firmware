#include <gtest/gtest.h>

#include <cstring>
#include <limits>
#include <string>

#include "booksync/services/PositionPolicy.h"
using namespace booksync;
#include "SharedCases.inc"
#include "booksync/services/Delivery.h"
#include "booksync/system/ReadingGate.h"
using namespace booksync::storage;
class PositionStateTest : public testing::Test {
 protected:
  PositionStore store{{"/place", "/place.tmp", "/place.bak"}};
  PositionRecord record{};
  SourceIdentity source{};
  // Host fixture owns the reusable worker buffer; never a firmware task local.
  char scratch[PositionStore::MaxBytes]{};
  void SetUp() override {
    card = {};
    source.book.bytes[0] = 1;
    source.edition.bytes[0] = 2;
    source.revision.bytes[0] = 3;
    record.scope = {"adapter:toy", "account:one"};
    record.source = source;
  }
  PositionValue remote(uint64_t at = 5000) {
    return {true, true, .7, at, 1, 1, "raw/remote", "Synthetic reader", false, false};
  }
  PositionInput geometry() {
    PositionInput i;
    i.local = 120;
    i.remote = 214;
    return i;
  }
  void ask(PositionSync& sync) {
    PositionDecision d{};
    PositionApply a{};
    ASSERT_EQ(sync.evaluate(record.token, geometry(), "open", d, a), PositionResult::Saved);
    EXPECT_EQ(d.action, PositionAction::Take);
    EXPECT_EQ(a, PositionApply::Ask);
  }
};
TEST_F(PositionStateTest, RawUnknownLocatorsAndAccountEditionIsolation) {
  PositionSync sync(store, record);
  auto value = remote();
  value.raw = std::string_view("raw\0future", 10);
  value.kind = 65535;
  value.version = 99;
  // Raw source bytes are binary, including NUL; labels/scope remain text.
  ASSERT_EQ(sync.observe(record.scope, source, value), PositionResult::Saved);
  card.restart();
  ASSERT_EQ(store.load({"adapter:toy", "account:one"}, source, record, scratch, sizeof(scratch)), PositionLoad::Ready);
  EXPECT_EQ(record.remote.raw, value.raw);
  EXPECT_EQ(record.remote.kind, 65535);
  EXPECT_EQ(store.load({"adapter:toy", "account:two"}, source, record, scratch, sizeof(scratch)),
            PositionLoad::Conflict);
  auto other = source;
  other.edition.bytes[0] = 4;
  EXPECT_EQ(store.load({"adapter:toy", "account:one"}, other, record, scratch, sizeof(scratch)),
            PositionLoad::Conflict);
  EXPECT_EQ(sync.observe({"adapter:toy", "account:one"}, other, remote()), PositionResult::Conflict);
}
TEST_F(PositionStateTest, StaleAndDuplicateAnswerNeverMoveAndNoPersistsAcrossRestart) {
  PositionSync sync(store, record);
  ASSERT_EQ(sync.observe(record.scope, source, remote()), PositionResult::Saved);
  ask(sync);
  const auto old = record.token;
  ASSERT_EQ(sync.observe(record.scope, source, remote(5200)), PositionResult::Saved);
  EXPECT_EQ(sync.answer(old, true), PositionResult::Stale);
  ask(sync);
  ASSERT_EQ(sync.answer(record.token, false), PositionResult::Saved);
  EXPECT_FALSE(record.allowPush);
  EXPECT_EQ(record.declinedAt, 5200);
  EXPECT_EQ(sync.answer(record.token, true), PositionResult::Stale);
  card.restart();
  ASSERT_EQ(store.load({"adapter:toy", "account:one"}, source, record, scratch, sizeof(scratch)), PositionLoad::Ready);
  PositionDecision d{};
  PositionApply a{};
  ASSERT_EQ(sync.evaluate(record.token, geometry(), "wake", d, a), PositionResult::Saved);
  EXPECT_EQ(d.action, PositionAction::Stay);
  EXPECT_EQ(a, PositionApply::No);
  EXPECT_FALSE(sync.mayDeliver(.4, 6000));
}
TEST_F(PositionStateTest, ResetDeclineSurvivesResetRedatingButEndsAfterNonreset) {
  PositionSync sync(store, record);
  auto r = remote();
  r.reset = true;
  r.percentage = 0;
  ASSERT_EQ(sync.observe(record.scope, source, r), PositionResult::Saved);
  auto i = geometry();
  i.remote = 0;
  PositionDecision d{};
  PositionApply a{};
  ASSERT_EQ(sync.evaluate(record.token, i, "open", d, a), PositionResult::Saved);
  ASSERT_EQ(sync.answer(record.token, false), PositionResult::Saved);
  r.at++;
  ASSERT_EQ(sync.observe(record.scope, source, r), PositionResult::Saved);
  ASSERT_EQ(sync.evaluate(record.token, i, "open", d, a), PositionResult::Saved);
  EXPECT_EQ(d.action, PositionAction::Stay);
  r.reset = false;
  ASSERT_EQ(sync.observe(record.scope, source, r), PositionResult::Saved);
  EXPECT_FALSE(record.declinedReset);
}
TEST_F(PositionStateTest, SleepDefersWakeLocallyCloseDoesNotMove) {
  PositionSync sync(store, record);
  ASSERT_EQ(sync.observe(record.scope, source, remote()), PositionResult::Saved);
  PositionDecision d{};
  PositionApply a{};
  ASSERT_EQ(sync.evaluate(record.token, geometry(), "sleep", d, a), PositionResult::Saved);
  EXPECT_TRUE(record.pending);
  EXPECT_EQ(a, PositionApply::Wake);
  card.restart();
  ASSERT_EQ(store.load({"adapter:toy", "account:one"}, source, record, scratch, sizeof(scratch)), PositionLoad::Ready);
  ASSERT_EQ(sync.evaluate(record.token, geometry(), "wake-local", d, a), PositionResult::Saved);
  EXPECT_EQ(a, PositionApply::Ask);
  ASSERT_EQ(sync.answer(record.token, true), PositionResult::Saved);
  EXPECT_EQ(record.marker, record.remote);
  ASSERT_EQ(sync.evaluate(record.token, geometry(), "close", d, a), PositionResult::Saved);
  EXPECT_EQ(a, PositionApply::No);
  EXPECT_EQ(sync.answer(record.token, true), PositionResult::Stale);
}
TEST_F(PositionStateTest, OverrideChosenBytesAndOlderCapturesAndReread) {
  PositionSync sync(store, record);
  ASSERT_EQ(sync.observe(record.scope, source, remote()), PositionResult::Saved);
  auto chosen = remote(5500);
  chosen.percentage = .05;
  chosen.raw = "raw/chosen";
  ASSERT_EQ(sync.overrideHere(chosen), PositionResult::Saved);
  PositionDecision d{};
  PositionApply a{};
  ASSERT_EQ(sync.evaluate(record.token, geometry(), "sleep", d, a), PositionResult::Saved);
  EXPECT_EQ(d.action, PositionAction::Override);
  EXPECT_TRUE(sync.mayDeliver(.05, 5500));
  EXPECT_EQ(record.overridePlace.raw, "raw/chosen");
  ASSERT_EQ(sync.sent(chosen, true), PositionResult::Saved);
  EXPECT_EQ(record.lastOverrideAt, 5500);
  EXPECT_FALSE(sync.mayDeliver(.9, 5400, true));
  ASSERT_EQ(sync.observe(record.scope, source, remote(5600)), PositionResult::Saved);
  auto i = geometry();
  i.finished = true;
  i.released = true;
  ASSERT_EQ(sync.evaluate(record.token, i, "open", d, a), PositionResult::Saved);
  EXPECT_EQ(d.action, PositionAction::Push);
  EXPECT_TRUE(sync.mayDeliver(.1, 5700));
}
TEST_F(PositionStateTest, RemoteMoveDropsQueuedOverride) {
  PositionSync sync(store, record);
  ASSERT_EQ(sync.observe(record.scope, source, remote()), PositionResult::Saved);
  record.marker = record.remote;
  ASSERT_EQ(sync.overrideHere(remote(5100)), PositionResult::Saved);
  ASSERT_EQ(sync.observe(record.scope, source, remote(5200)), PositionResult::Saved);
  PositionDecision d{};
  PositionApply a{};
  auto i = geometry();
  i.marker = PositionMarker{5000, 120};
  ASSERT_EQ(sync.evaluate(record.token, i, "open", d, a), PositionResult::Saved);
  EXPECT_TRUE(d.dropped);
  EXPECT_FALSE(record.overridePlace.present);
  EXPECT_FALSE(sync.mayDeliver(.1, 5300));
}
TEST_F(PositionStateTest, FailedSaveDoesNotAuthorizeMove) {
  PositionSync sync(store, record);
  ASSERT_EQ(sync.observe(record.scope, source, remote()), PositionResult::Saved);
  ask(sync);
  card.full = true;
  EXPECT_EQ(sync.answer(record.token, true), PositionResult::StorageError);
  card.full = false;
  card.restart();
  ASSERT_EQ(store.load({"adapter:toy", "account:one"}, source, record, scratch, sizeof(scratch)), PositionLoad::Ready);
  EXPECT_FALSE(record.marker.present);
  EXPECT_TRUE(record.answerable);
}
TEST_F(PositionStateTest, SameBookLateConsentDrawsExactlyOnceAfterCallerTimeout) {
  PositionSync sync(store, record);
  int calls = 0;
  Delivery completion({1, 1}, 0, DeliveryPolicy::Owner,
                      {[](void* p, DeliveryResult) { ++*static_cast<int*>(p); }, &calls});
  PositionPresentation consent({1, 1}, {10, 20, 30});
  completion.poll(120000, DeliveryResult::Pending, {1, 1});
  EXPECT_EQ(calls, 1);
  ASSERT_EQ(sync.observe(record.scope, source, remote()), PositionResult::Saved);
  ask(sync);
  EXPECT_TRUE(consent.claim({1, 1}, {10, 20, 30}, {true, true, false}));
  EXPECT_FALSE(consent.claim({1, 1}, {10, 20, 30}, {true, true, false}));
  EXPECT_EQ(sync.answer(record.token, true), PositionResult::Saved);
  EXPECT_EQ(sync.answer(record.token, true), PositionResult::Stale);
  completion.poll(130000, DeliveryResult::Ready, {1, 1});
  EXPECT_EQ(calls, 1);
}
TEST_F(PositionStateTest, HiddenAndStaleDocumentModeAccountRejectConsent) {
  for (auto now : {PositionContext{11, 20, 30}, PositionContext{10, 21, 30}, PositionContext{10, 20, 31},
                   PositionContext{10, 20, 30, false}}) {
    PositionPresentation gate({1, 1}, {10, 20, 30});
    EXPECT_FALSE(gate.claim({1, 1}, now, {true, true, false}));
    EXPECT_FALSE(gate.claim({1, 1}, {10, 20, 30}, {true, true, false}));
  }
  PositionPresentation gate({1, 1}, {10, 20, 30});
  EXPECT_FALSE(gate.claim({1, 1}, {10, 20, 30}, {true, false, false}));
  EXPECT_FALSE(gate.claim({1, 1}, {10, 20, 30}, {true, true, true}));
  EXPECT_FALSE(gate.claim({1, 2}, {10, 20, 30}, {true, true, false}));
}
TEST_F(PositionStateTest, CorruptionUnknownVersionAndCollisionRetainBytes) {
  ASSERT_TRUE(store.save(record));
  auto original = card.files;
  auto other = record;
  other.scope.account = "account:two";
  EXPECT_FALSE(store.save(other));
  EXPECT_EQ(card.files, original);
  card.files["/place"][0] = 'X';
  auto bad = card.files;
  EXPECT_EQ(store.load(record.scope, source, record, scratch, sizeof(scratch)), PositionLoad::Unreadable);
  EXPECT_FALSE(store.save(record));
  EXPECT_EQ(card.files, bad);
}
TEST_F(PositionStateTest, EveryPublicationFailureRecoversCompleteOldOrNewState) {
  record.token = 1;
  ASSERT_TRUE(store.save(record));
  auto initial = card;
  record.token = 2;
  ASSERT_TRUE(store.save(record));
  const size_t steps = card.events.size() - initial.events.size();
  for (size_t cut = 0; cut < steps; ++cut) {
    card = initial;
    card.events.clear();
    card.stop = cut;
    card.eager = true;
    try {
      store.save(record);
    } catch (const PowerCut&) {
    }
    card.restart();
    PositionRecord loaded;
    auto result = store.load({"adapter:toy", "account:one"}, source, loaded, scratch, sizeof(scratch));
    ASSERT_EQ(result, PositionLoad::Ready) << cut;
    EXPECT_TRUE(loaded.token == 1 || loaded.token == 2) << cut;
  }
}
TEST(PositionCustom, ApprovedStrategiesAndNoSilentAskAfterRender) {
  PositionInput i;
  i.local = 120;
  i.remote = 214;
  i.remoteAt = 5000;
  PositionMode mode{false, false, PositionStrategy::Ask, PositionStrategy::Keep};
  auto d = PositionPolicy::decide(i, mode);
  EXPECT_EQ(d.action, PositionAction::Take);
  EXPECT_EQ(PositionPolicy::apply(d, "pause", mode), PositionApply::No);
  EXPECT_EQ(PositionPolicy::apply(d, "prerender", mode), PositionApply::No);
  EXPECT_EQ(PositionPolicy::apply(d, "open", mode), PositionApply::Ask);
  mode.ahead = PositionStrategy::Follow;
  d = PositionPolicy::decide(i, mode);
  EXPECT_EQ(PositionPolicy::apply(d, "prerender", mode), PositionApply::Now);
  EXPECT_EQ(PositionPolicy::apply(d, "reading", mode), PositionApply::Ask);
  mode.ahead = PositionStrategy::Keep;
  EXPECT_EQ(PositionPolicy::decide(i, mode).action, PositionAction::Hold);
}
TEST_F(PositionStateTest, InitialAbsentRemoteAndInvalidGeometryAndTokenExhaustion) {
  PositionSync sync(store, record);
  ASSERT_EQ(sync.observe(record.scope, source, {}), PositionResult::Saved);
  EXPECT_NE(record.token, 0);
  PositionDecision d{};
  PositionApply a{};
  auto i = geometry();
  ASSERT_EQ(sync.evaluate(record.token, i, "open", d, a), PositionResult::Saved);
  EXPECT_EQ(d.action, PositionAction::Push);
  i.local = std::numeric_limits<double>::quiet_NaN();
  EXPECT_EQ(sync.evaluate(record.token, i, "open", d, a), PositionResult::Invalid);
  record.token = UINT64_MAX;
  EXPECT_EQ(sync.observe(record.scope, source, remote()), PositionResult::Invalid);
}
TEST_F(PositionStateTest, StorageFailurePoisonsSessionUntilReloadAndReconstruction) {
  PositionSync sync(store, record);
  ASSERT_EQ(sync.observe(record.scope, source, remote()), PositionResult::Saved);
  ask(sync);
  card.full = true;
  EXPECT_EQ(sync.answer(record.token, true), PositionResult::StorageError);
  card.full = false;
  EXPECT_EQ(sync.observe(record.scope, source, remote()), PositionResult::StorageError);
  EXPECT_FALSE(sync.mayDeliver(.9, 6000, true));
}
TEST_F(PositionStateTest, ConfirmedSendPublicationIsIdempotentAfterJournalCut) {
  PositionSync sync(store, record);
  ASSERT_EQ(sync.observe(record.scope, source, remote()), PositionResult::Saved);
  auto chosen = remote(5500);
  chosen.percentage = .05;
  ASSERT_EQ(sync.overrideHere(chosen), PositionResult::Saved);
  ASSERT_EQ(sync.sent(chosen, true), PositionResult::Saved);
  const auto token = record.token;
  card.restart();
  ASSERT_EQ(store.load({"adapter:toy", "account:one"}, source, record, scratch, sizeof(scratch)), PositionLoad::Ready);
  PositionSync restarted(store, record);
  EXPECT_EQ(restarted.sent(chosen, true), PositionResult::Saved);
  EXPECT_EQ(record.token, token);
  EXPECT_EQ(record.lastOverrideAt, 5500);
}
TEST_F(PositionStateTest, MaximumThreeLocatorsAndReadOnlyCardAreBounded) {
  static char raw[PositionStore::MaxRaw];
  std::memset(raw, 'x', sizeof(raw));
  auto value = remote();
  value.raw = {raw, sizeof(raw)};
  record.remote = record.marker = record.overridePlace = value;
  record.token = 1;
  ASSERT_TRUE(store.save(record));
  EXPECT_LE(card.files["/place"].size(), PositionStore::MaxBytes);
  card.restart();
  ASSERT_EQ(store.load({"adapter:toy", "account:one"}, source, record, scratch, sizeof(scratch)), PositionLoad::Ready);
  EXPECT_EQ(record.overridePlace.raw.size(), sizeof(raw));
  auto before = card.files;
  card.readOnly = true;
  record.token++;
  EXPECT_FALSE(store.save(record));
  EXPECT_EQ(card.files, before);
}
TEST_F(PositionStateTest, UnknownFlagsWithValidChecksumCannotBeReplaced) {
  ASSERT_TRUE(store.save(record));
  auto& bytes = card.files["/place"];
  const size_t flags = 16 + 2 + record.scope.server.size() + 2 + record.scope.account.size() + 64 + 32;
  bytes[flags] = char(0x80);
  uint32_t hash = 2166136261U;
  for (size_t i = 16; i < bytes.size(); ++i) hash = (hash ^ static_cast<uint8_t>(bytes[i])) * 16777619U;
  for (unsigned i = 0; i < 4; ++i) bytes[8 + i] = char(hash >> (i * 8));
  const auto unknown = card.files;
  EXPECT_EQ(store.load(record.scope, source, record, scratch, sizeof(scratch)), PositionLoad::Unreadable);
  EXPECT_FALSE(store.save(record));
  EXPECT_EQ(card.files, unknown);
}
TEST_F(PositionStateTest, ReloadBindingCheckPrecedesOverwriteOfAliasedExpectedValues) {
  ASSERT_TRUE(store.save(record));
  ASSERT_EQ(store.load(record.scope, record.source, record, scratch, sizeof(scratch)), PositionLoad::Ready);
  const auto saved = record.scope;
  // Replace the complete file with a valid record for a different account.
  // Simulates a path/index collision while preserving all integrity checks.
  PositionRecord foreign;
  foreign.scope = {"adapter:toy", "account:two"};
  foreign.source = source;
  PositionStore other{{"/other", "/other.tmp", "/other.bak"}};
  ASSERT_TRUE(other.save(foreign));
  card.files["/place"] = card.files["/other"];
  EXPECT_EQ(store.load(record.scope, record.source, record, scratch, sizeof(scratch)), PositionLoad::Conflict);
  EXPECT_EQ(record.scope, saved);
  EXPECT_EQ(record.scope.account, "account:one");
}
TEST_F(PositionStateTest, MissingLoadPreservesAliasedInitialIdentity) {
  EXPECT_EQ(store.load(record.scope, record.source, record, scratch, sizeof(scratch)), PositionLoad::Missing);
  EXPECT_EQ(record.source, source);
  EXPECT_EQ(record.scope.account, "account:one");
}
TEST_F(PositionStateTest, CustomComparisonPersistsAndOpenResetsIt) {
  PositionSync sync(store, record);
  ASSERT_EQ(sync.observe(record.scope, source, remote()), PositionResult::Saved);
  auto i = geometry();
  i.turnAt = 6000;
  PositionMode mode{false, false, PositionStrategy::Ask, PositionStrategy::Keep};
  PositionDecision d{};
  PositionApply a{};
  ASSERT_EQ(sync.evaluate(record.token, i, "open", d, a, mode), PositionResult::Saved);
  EXPECT_EQ(d.action, PositionAction::Take);
  EXPECT_TRUE(record.compared);
  EXPECT_FALSE(sync.mayDeliver(.4, 6000, true));
  EXPECT_TRUE(sync.mayDeliver(.4, 6000, true, true));
  card.restart();
  ASSERT_EQ(store.load({"adapter:toy", "account:one"}, source, record, scratch, sizeof(scratch)), PositionLoad::Ready);
  PositionSync restarted(store, record);
  ASSERT_EQ(restarted.evaluate(record.token, i, "reading", d, a, mode), PositionResult::Saved);
  EXPECT_EQ(d.action, PositionAction::Push);
  EXPECT_TRUE(restarted.mayDeliver(.4, 6000, true));
  ASSERT_EQ(restarted.opened(), PositionResult::Saved);
  ASSERT_EQ(restarted.evaluate(record.token, i, "open", d, a, mode), PositionResult::Saved);
  EXPECT_EQ(d.action, PositionAction::Take);
}
TEST_F(PositionStateTest, RefreshKeepsSleepReminderUntilTheNewPlaceIsDecided) {
  PositionSync sync(store, record);
  ASSERT_EQ(sync.observe(record.scope, source, remote()), PositionResult::Saved);
  PositionDecision d{};
  PositionApply a{};
  ASSERT_EQ(sync.evaluate(record.token, geometry(), "sleep", d, a), PositionResult::Saved);
  ASSERT_TRUE(record.pending);
  ASSERT_EQ(sync.observe(record.scope, source, remote(5200)), PositionResult::Saved);
  EXPECT_TRUE(record.pending);
  EXPECT_FALSE(record.answerable);
  card.restart();
  ASSERT_EQ(store.load({"adapter:toy", "account:one"}, source, record, scratch, sizeof(scratch)), PositionLoad::Ready);
  PositionSync restarted(store, record);
  ASSERT_EQ(restarted.evaluate(record.token, geometry(), "wake-local", d, a), PositionResult::Saved);
  EXPECT_EQ(a, PositionApply::Ask);
}
TEST_F(PositionStateTest, ReleasedRereadDistinguishesZeroRemoteTimeFromNoRelease) {
  PositionSync sync(store, record);
  ASSERT_EQ(sync.observe(record.scope, source, remote(0)), PositionResult::Saved);
  auto i = geometry();
  i.finished = true;
  i.released = true;
  PositionDecision d{};
  PositionApply a{};
  ASSERT_EQ(sync.evaluate(record.token, i, "open", d, a), PositionResult::Saved);
  EXPECT_EQ(d.action, PositionAction::Push);
  EXPECT_TRUE(sync.mayDeliver(.1, 6000));
  card.restart();
  ASSERT_EQ(store.load({"adapter:toy", "account:one"}, source, record, scratch, sizeof(scratch)), PositionLoad::Ready);
  PositionSync restarted(store, record);
  EXPECT_TRUE(restarted.mayDeliver(.1, 6000));
}
TEST_F(PositionStateTest, FullF08ScopeTokensRemainUsable) {
  std::string server(128, 's'), account(128, 'a');
  record.scope = {server, account};
  ASSERT_TRUE(store.save(record));
  ASSERT_EQ(store.load({server, account}, source, record, scratch, sizeof(scratch)), PositionLoad::Ready);
  EXPECT_EQ(record.scope.server, server);
  EXPECT_EQ(record.scope.account, account);
}
TEST_F(PositionStateTest, ReplayedSendConfirmsVisibleButUnflushedPublication) {
  PositionSync initial(store, record);
  ASSERT_EQ(initial.observe(record.scope, source, remote()), PositionResult::Saved);
  auto chosen = remote(5500);
  chosen.percentage = .05;
  chosen.raw = "raw/chosen";
  ASSERT_EQ(initial.overrideHere(chosen), PositionResult::Saved);
  const auto durableBefore = card;
  card.events.clear();
  ASSERT_EQ(initial.sent(chosen, true), PositionResult::Saved);
  int barrier = -1;
  for (size_t i = 0; i < card.events.size(); ++i)
    if (card.events[i] == "fs-sync:before") barrier = static_cast<int>(i);
  ASSERT_GE(barrier, 0);
  card = durableBefore;
  ASSERT_EQ(store.load({"adapter:toy", "account:one"}, source, record, scratch, sizeof(scratch)), PositionLoad::Ready);
  card.events.clear();
  card.fail = barrier;
  PositionSync interrupted(store, record);
  ASSERT_EQ(interrupted.sent(chosen, true), PositionResult::StorageError);
  ASSERT_NE(card.files, card.disk);
  card.fail = -1;
  ASSERT_EQ(store.load({"adapter:toy", "account:one"}, source, record, scratch, sizeof(scratch)), PositionLoad::Ready);
  PositionSync retry(store, record);
  ASSERT_EQ(retry.sent(chosen, true), PositionResult::Saved);
  card.restart();
  ASSERT_EQ(store.load({"adapter:toy", "account:one"}, source, record, scratch, sizeof(scratch)), PositionLoad::Ready);
  EXPECT_EQ(record.lastOverrideAt, 5500);
  EXPECT_FALSE(record.overridePlace.present);
}
TEST_F(PositionStateTest, EqualNonresetObservationEndsTheOldResetDecline) {
  PositionSync sync(store, record);
  auto reset = remote();
  reset.reset = true;
  reset.percentage = 0;
  ASSERT_EQ(sync.observe(record.scope, source, reset), PositionResult::Saved);
  auto i = geometry();
  i.remote = 0;
  PositionDecision d{};
  PositionApply a{};
  ASSERT_EQ(sync.evaluate(record.token, i, "open", d, a), PositionResult::Saved);
  ASSERT_EQ(sync.answer(record.token, false), PositionResult::Saved);
  ASSERT_TRUE(record.declinedReset);
  auto committed = remote(5500);
  committed.percentage = .5;
  committed.raw = "raw/local";
  ASSERT_TRUE(sync.mayDeliver(.5, 5500, true, true));
  ASSERT_EQ(sync.sent(committed, false), PositionResult::Saved);
  ASSERT_EQ(sync.observe(record.scope, source, committed), PositionResult::Saved);
  EXPECT_FALSE(record.declinedReset);
  reset.at = 6000;
  ASSERT_EQ(sync.observe(record.scope, source, reset), PositionResult::Saved);
  ASSERT_EQ(sync.evaluate(record.token, i, "open", d, a), PositionResult::Saved);
  EXPECT_EQ(d.action, PositionAction::Follow);
}
