// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>

#include "booksync/services/Delivery.h"
#include "booksync/storage/LifecycleJournal.h"
#include "booksync/system/ReadingGate.h"

// Synthetic view/clock/worker with production tickets, gate and durable journal.
// No new product screen or reader operation is introduced by this policy slice.
inline void runDeliveryScenario() {
  if (!std::getenv("BOOKSYNC_DELIVERY_SCENARIO")) return;
  using namespace booksync;
  using namespace booksync::storage;
  struct View {
    int values = 0, paints = 0;
    PresentationState state{true, true, false};
  } view;
  const DeliveryCallback done{[](void* p, DeliveryResult result) {
                                auto& v = *static_cast<View*>(p);
                                if (result != DeliveryResult::Ready) return;
                                ++v.values;
                                if (ReadingGate::mayShow(Presentation::Sync, v.state)) ++v.paints;
                              },
                              &view};
  LifecycleJournal journal({"/delivery-job", "/delivery-job.tmp", "/delivery-job.bak"});
  uint64_t job = 0;
  bool ok = journal.recover() == JournalResult::Ok &&
            journal.enqueue({0, "toy", "account-a", "document", "work", "pending", 0}, job) == JournalResult::Ok;
  DeliveryOwner account;
  account.adopt(1);
  const auto original = account.current();
  Delivery closed(original, 0, DeliveryPolicy::Owner, done);
  closed.retire();
  closed.poll(1, DeliveryResult::Ready, original);
  ok = ok && view.values == 0 && !closed.polling() && journal.pending() == 1 &&
       journal.checkpoint(job, "still-running") == JournalResult::Ok;
  Delivery switched(original, 0, DeliveryPolicy::Owner, done);
  account.adopt(2);
  account.adopt(1);
  switched.poll(1, DeliveryResult::Ready, account.current());
  ok = ok && view.values == 0 && !switched.polling();
  Delivery late(account.current(), 0, DeliveryPolicy::Owner, done);
  late.poll(130000, DeliveryResult::Ready, account.current());
  late.poll(130001, DeliveryResult::Ready, account.current());
  ok = ok && view.values == 1 && view.paints == 0;
  Delivery expired(account.current(), 0, DeliveryPolicy::Owner, done);
  expired.poll(120000, DeliveryResult::Pending, account.current());
  expired.poll(130000, DeliveryResult::Ready, account.current());
  ok = ok && view.values == 1 && !expired.polling();
  PositionPresentation consent(account.current(), {1, 1, 1});
  ok = ok && consent.claim(account.current(), {1, 1, 1}, view.state) &&
       !consent.claim(account.current(), {1, 1, 1}, view.state);
  PositionPresentation superseded(account.current(), {1, 1, 1});
  ok = ok && !superseded.claim(account.current(), {1, 1, 2}, view.state) &&
       !superseded.claim(account.current(), {1, 1, 1}, view.state);
  view.state.visible = false;
  Delivery hidden(account.current(), 0, DeliveryPolicy::Owner, done);
  hidden.poll(1, DeliveryResult::Ready, account.current());
  ok = ok && view.values == 2 && view.paints == 0;
  Delivery device({}, 0, DeliveryPolicy::Device, done, done);
  device.retire();
  device.poll(1, DeliveryResult::Ready, {});
  ok = ok && view.values == 3 && view.paints == 0;
  LOG_INF("BSD", "BookSync delivery scenario %s: 8 policy checks; zero presentation repaints", ok ? "PASS" : "FAIL");
  std::fflush(nullptr);
  std::_Exit(ok ? 0 : 1);
}
