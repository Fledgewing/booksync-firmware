// SPDX-License-Identifier: GPL-3.0-or-later
#include <HalStorage.h>
#include <gtest/gtest.h>

#include "booksync/services/Delivery.h"
#include "booksync/storage/LifecycleJournal.h"
#include "booksync/system/ReadingGate.h"
using namespace booksync;
struct Consumer {
  int calls = 0;
  DeliveryResult result = DeliveryResult::Pending;
  static void receive(void* context, DeliveryResult result) {
    auto& self = *static_cast<Consumer*>(context);
    ++self.calls;
    self.result = result;
  }
  DeliveryCallback callback() { return {receive, this}; }
};
TEST(Delivery, SettlesOnce) {
  Consumer c;
  Delivery d({1, 1}, 0, DeliveryPolicy::Owner, c.callback());
  d.poll(1, DeliveryResult::Ready, {1, 1});
  d.poll(2, DeliveryResult::Ready, {1, 1});
  d.retire();
  EXPECT_EQ(c.calls, 1);
  EXPECT_FALSE(d.polling());
}
TEST(Delivery, RetirementDoesNotCancelDurableJob) {
  Consumer c;
  using namespace booksync::storage;
  card = {};
  LifecycleJournal journal({"/job", "/job.tmp", "/job.bak"});
  ASSERT_EQ(journal.recover(), JournalResult::Ok);
  uint64_t id = 0;
  ASSERT_EQ(journal.enqueue({0, "toy", "account-a", "document", "work", "pending", 0}, id), JournalResult::Ok);
  Delivery d({1, 1}, 0, DeliveryPolicy::Owner, c.callback());
  d.retire();
  d.retire();
  d.poll(1, DeliveryResult::Ready, {1, 1});
  EXPECT_EQ(c.calls, 0);
  EXPECT_EQ(journal.checkpoint(id, "still-running"), JournalResult::Ok);
  card.restart();
  LifecycleJournal restarted({"/job", "/job.tmp", "/job.bak"});
  ASSERT_EQ(restarted.recover(), JournalResult::Ok);
  EXPECT_EQ(restarted.pending(), 1U);
  uint64_t found = 0;
  EXPECT_EQ(restarted.next("toy", "account-a", 0, found), JournalResult::Ok);
  EXPECT_EQ(found, id);
  EXPECT_FALSE(d.polling());
}
TEST(Delivery, OwnerAndEpochBothRequired) {
  for (auto current : {DeliveryTicket{2, 1}, DeliveryTicket{1, 3}}) {
    Consumer c;
    Delivery d({1, 1}, 0, DeliveryPolicy::Owner, c.callback());
    d.poll(1, DeliveryResult::Ready, current);
    EXPECT_EQ(c.result, DeliveryResult::Canceled);
    d.poll(2, DeliveryResult::Ready, {1, 1});
    EXPECT_EQ(c.calls, 1);
  }
}
TEST(Delivery, AccountABA) {
  DeliveryOwner owner;
  EXPECT_TRUE(owner.adopt(1));
  auto original = owner.current();
  EXPECT_TRUE(owner.adopt(1));
  EXPECT_EQ(original.epoch, owner.current().epoch);
  owner.adopt(2);
  owner.adopt(1);
  EXPECT_NE(original.epoch, owner.current().epoch);
  Consumer c;
  Delivery d(original, 0, DeliveryPolicy::Owner, c.callback());
  d.poll(1, DeliveryResult::Ready, owner.current());
  EXPECT_EQ(c.result, DeliveryResult::Canceled);
}
TEST(Delivery, EligibilityCannotRevive) {
  Consumer c;
  Delivery d({1, 1}, 0, DeliveryPolicy::Owner, c.callback());
  d.poll(1, DeliveryResult::Pending, {1, 1}, false);
  d.poll(2, DeliveryResult::Ready, {1, 1}, true);
  EXPECT_EQ(c.calls, 1);
  EXPECT_EQ(c.result, DeliveryResult::Canceled);
}
TEST(Delivery, LateReadyBeforeDeadlineCheck) {
  Consumer c;
  Delivery d({1, 1}, 10, DeliveryPolicy::Owner, c.callback());
  d.poll(130010, DeliveryResult::Ready, {1, 1});
  EXPECT_EQ(c.result, DeliveryResult::Ready);
}
TEST(Delivery, PendingAtDeadlineIsTerminal) {
  for (auto now : {120010ULL, 130010ULL}) {
    Consumer c;
    Delivery d({1, 1}, 10, DeliveryPolicy::Owner, c.callback());
    d.poll(120009, DeliveryResult::Pending, {1, 1});
    EXPECT_EQ(c.calls, 0);
    d.poll(now, DeliveryResult::Pending, {1, 1});
    d.poll(now + 1, DeliveryResult::Ready, {1, 1});
    EXPECT_EQ(c.calls, 1);
    EXPECT_EQ(c.result, DeliveryResult::Unavailable);
  }
}
TEST(Delivery, ErrorsAndSynchronousRejection) {
  for (auto result : {DeliveryResult::Invalid, DeliveryResult::Unavailable}) {
    Consumer c;
    Delivery d({1, 1}, 0, DeliveryPolicy::Owner, c.callback());
    d.poll(0, result, {1, 1});
    d.retire();
    EXPECT_EQ(c.calls, 1);
    EXPECT_EQ(c.result, result);
  }
}
TEST(Delivery, DeviceEffectSurvivesClosedPresentationAndOwnerChange) {
  Consumer view, effect;
  Delivery d({}, 0, DeliveryPolicy::Device, view.callback(), effect.callback());
  d.retire();
  EXPECT_TRUE(d.polling());
  d.poll(1, DeliveryResult::Ready, {2, 4});
  d.poll(2, DeliveryResult::Ready, {2, 4});
  EXPECT_EQ(view.calls, 0);
  EXPECT_EQ(effect.calls, 1);
}
TEST(Delivery, ReentrantSettlementIsSafe) {
  struct State {
    Delivery* ticket;
    int calls = 0;
  } state{};
  Delivery d({1, 1}, 0, DeliveryPolicy::Owner,
             {[](void* p, DeliveryResult) {
                auto& s = *static_cast<State*>(p);
                ++s.calls;
                s.ticket->poll(0, DeliveryResult::Ready, {1, 1});
                s.ticket->retire();
              },
              &state});
  state.ticket = &d;
  d.poll(0, DeliveryResult::Ready, {1, 1});
  EXPECT_EQ(state.calls, 1);
}
TEST(ReadingGate, QuietWhileReadingAndFailClosed) {
  PresentationState state{true, true, false};
  for (auto event : {Presentation::Message, Presentation::Update, Presentation::Network, Presentation::Sync,
                     Presentation::Restart, Presentation::Navigation, Presentation::BackgroundRender})
    EXPECT_FALSE(ReadingGate::mayShow(event, state));
  EXPECT_TRUE(ReadingGate::mayShow(Presentation::UserOpened, state));
  EXPECT_TRUE(ReadingGate::mayShow(Presentation::EndOfBook, state));
  EXPECT_FALSE(ReadingGate::mayShow(static_cast<Presentation>(255), state));
}
TEST(ReadingGate, HiddenCloseAndSleepNeverRepaint) {
  for (auto state : {PresentationState{false, false, false}, PresentationState{true, false, false},
                     PresentationState{true, true, true}})
    for (auto event : {Presentation::Message, Presentation::UserOpened, Presentation::PositionConsent})
      EXPECT_FALSE(ReadingGate::mayShow(event, state, true));
}
TEST(ReadingGate, HomeAllowsForegroundEffectsOnly) {
  EXPECT_TRUE(ReadingGate::mayShow(Presentation::Message, {false, true, false}));
  EXPECT_FALSE(ReadingGate::mayShow(Presentation::BackgroundRender, {false, true, false}));
  EXPECT_FALSE(ReadingGate::mayShow(Presentation::PositionConsent, {true, true, false}));
}
TEST(ReadingGate, LiveConsentSurvivesCallerTimeout) {
  Consumer caller;
  Delivery completion({1, 1}, 0, DeliveryPolicy::Owner, caller.callback());
  PositionPresentation consent({1, 1}, {10, 20, 30});
  completion.poll(120000, DeliveryResult::Pending, {1, 1});
  EXPECT_EQ(caller.result, DeliveryResult::Unavailable);
  EXPECT_TRUE(
      ReadingGate::mayShow(Presentation::PositionConsent, {true, true, false}, consent.eligible({1, 1}, {10, 20, 30})));
}
TEST(ReadingGate, ConsentRejectsDocumentPathRunOwnerEpochAndNeverRevives) {
  for (auto context : {PositionContext{11, 20, 30}, PositionContext{10, 21, 30}, PositionContext{10, 20, 31}}) {
    PositionPresentation consent({1, 1}, {10, 20, 30});
    EXPECT_FALSE(consent.eligible({1, 1}, context));
    EXPECT_FALSE(consent.eligible({1, 1}, {10, 20, 30}));
  }
  for (auto current : {DeliveryTicket{2, 1}, DeliveryTicket{1, 3}}) {
    PositionPresentation consent({1, 1}, {10, 20, 30});
    EXPECT_FALSE(consent.eligible(current, {10, 20, 30}));
  }
  PositionPresentation newer({1, 1}, {10, 20, 31});
  EXPECT_TRUE(newer.eligible({1, 1}, {10, 20, 31}));
  newer.retire();
  EXPECT_FALSE(newer.eligible({1, 1}, {10, 20, 31}));
}
TEST(Delivery, DeviceEffectCanCloseViewBeforePresentation) {
  Consumer view;
  Delivery* ticket = nullptr;
  Delivery d({}, 0, DeliveryPolicy::Device, view.callback(),
             {[](void* p, DeliveryResult) { (*static_cast<Delivery**>(p))->retire(); }, &ticket});
  ticket = &d;
  d.poll(0, DeliveryResult::Ready, {});
  EXPECT_EQ(view.calls, 0);
}
TEST(ReadingGate, OneDrawConsentAndModeGuards) {
  PositionPresentation consent({1, 1}, {10, 20, 30});
  EXPECT_FALSE(consent.claim({1, 1}, {10, 20, 30}, {true, true, true}));
  EXPECT_FALSE(consent.claim({1, 1}, {10, 20, 30}, {true, false, false}));
  EXPECT_TRUE(consent.claim({1, 1}, {10, 20, 30}, {true, true, false}));
  EXPECT_FALSE(consent.claim({1, 1}, {10, 20, 30}, {true, true, false}));
}
TEST(ReadingGate, ClosedAndSwitchedConsumersNeverRepaint) {
  struct View {
    int adopted = 0, paints = 0;
    PresentationState state{false, false, false};
  } view;
  auto callback = DeliveryCallback{[](void* p, DeliveryResult result) {
                                     auto& v = *static_cast<View*>(p);
                                     if (result != DeliveryResult::Ready) return;
                                     ++v.adopted;
                                     if (ReadingGate::mayShow(Presentation::Sync, v.state)) ++v.paints;
                                   },
                                   &view};
  Delivery hidden({1, 1}, 0, DeliveryPolicy::Owner, callback);
  hidden.poll(0, DeliveryResult::Ready, {1, 1});
  EXPECT_EQ(view.adopted, 1);
  EXPECT_EQ(view.paints, 0);
  view.state.visible = true;
  Delivery closed({1, 1}, 0, DeliveryPolicy::Owner, callback);
  closed.retire();
  closed.poll(0, DeliveryResult::Ready, {1, 1});
  Delivery switched({1, 1}, 0, DeliveryPolicy::Owner, callback);
  switched.poll(0, DeliveryResult::Ready, {2, 2});
  EXPECT_EQ(view.adopted, 1);
  EXPECT_EQ(view.paints, 0);
}
TEST(ReadingGate, ConsentRejectsChangedModePermanently) {
  PositionPresentation consent({1, 1}, {10, 20, 30, true});
  EXPECT_FALSE(consent.claim({1, 1}, {10, 20, 30, false}, {true, true, false}));
  EXPECT_FALSE(consent.claim({1, 1}, {10, 20, 30, true}, {true, true, false}));
}
TEST(ReadingGate, CloseAndSleepRunsCannotPromptAfterWake) {
  for (auto moment : {PositionMoment::Close, PositionMoment::Sleep}) {
    PositionPresentation consent({1, 1}, {10, 20, 30}, moment);
    EXPECT_FALSE(consent.claim({1, 1}, {10, 20, 30}, {true, true, false}));
  }
  PositionPresentation manual({1, 1}, {10, 20, 30, false});
  EXPECT_TRUE(manual.claim({1, 1}, {10, 20, 30, false}, {true, true, false}));
}
