// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "booksync/services/Delivery.h"
#include "booksync/services/PositionPolicy.h"
#include "booksync/system/ReadingGate.h"

inline void runPositionPolicyScenario() {
  const char* mode = std::getenv("BOOKSYNC_POSITION_SCENARIO");
  if (!mode) return;
  using namespace booksync;
  using namespace booksync::storage;
  static char scratch[PositionStore::MaxBytes];
  static PositionRecord record;
  SourceIdentity source{};
  source.book.bytes[0] = 1;
  source.edition.bytes[0] = 2;
  source.revision.bytes[0] = 3;
  const ScopeView scope{"toy:position", "account:one"};
  PositionStore store{{"/position/place", "/position/place.tmp", "/position/place.bak"}};
  const auto loaded = store.load(scope, source, record, scratch, sizeof(scratch));
  bool ok = loaded == PositionLoad::Ready || loaded == PositionLoad::Missing;
  PositionSync sync(store, record);
  PositionValue remote{true, true, .7, 5000, 65535, 99, "unknown/raw/remote", "Synthetic reader", false, false};
  PositionInput geometry;
  geometry.local = 120;
  geometry.remote = 214;
  PositionDecision decision{};
  PositionApply applied{};
  int draws = 0, completions = 0;
  if (!std::strcmp(mode, "seed")) {
    ok = ok && sync.observe(scope, source, remote) == PositionResult::Saved;
  } else if (!std::strcmp(mode, "late")) {
    Delivery completion({1, 1}, 0, DeliveryPolicy::Owner,
                        {[](void* p, DeliveryResult) { ++*static_cast<int*>(p); }, &completions});
    PositionPresentation consent({1, 1}, {10, 20, 30});
    completion.poll(120000, DeliveryResult::Pending, {1, 1});
    ok = ok && completions == 1 &&
         sync.evaluate(record.token, geometry, "open", decision, applied) == PositionResult::Saved &&
         decision.action == PositionAction::Take && applied == PositionApply::Ask;
    if (consent.claim({1, 1}, {10, 20, 30}, {true, true, false})) ++draws;
    if (consent.claim({1, 1}, {10, 20, 30}, {true, true, false})) ++draws;
    completion.poll(130000, DeliveryResult::Ready, {1, 1});
    ok = ok && draws == 1 && completions == 1 && sync.answer(record.token, false) == PositionResult::Saved;
  } else if (!std::strcmp(mode, "declined")) {
    ok = ok && record.declinedAt == 5000 &&
         sync.evaluate(record.token, geometry, "wake-local", decision, applied) == PositionResult::Saved &&
         decision.action == PositionAction::Stay && applied == PositionApply::No && !sync.mayDeliver(.4, 6000);
  } else if (!std::strcmp(mode, "stale")) {
    const auto old = record.token;
    remote.at = 5200;
    ok = ok && sync.observe(scope, source, remote) == PositionResult::Saved &&
         sync.answer(old, true) == PositionResult::Stale &&
         sync.evaluate(old, geometry, "open", decision, applied) == PositionResult::Stale;
    auto other = source;
    other.edition.bytes[0] = 4;
    ok = ok && sync.observe(scope, other, remote) == PositionResult::Conflict;
    for (auto context : {PositionContext{11, 20, 30}, PositionContext{10, 21, 30}, PositionContext{10, 20, 31},
                         PositionContext{10, 20, 30, false}}) {
      PositionPresentation gate({1, 1}, {10, 20, 30});
      ok = ok && !gate.claim({1, 1}, context, {true, true, false}) &&
           !gate.claim({1, 1}, {10, 20, 30}, {true, true, false});
    }
  } else if (!std::strcmp(mode, "sleep")) {
    ok = ok && sync.evaluate(record.token, geometry, "sleep", decision, applied) == PositionResult::Saved &&
         record.pending && applied == PositionApply::Wake;
  } else if (!std::strcmp(mode, "refresh-sleep")) {
    remote.at = 5200;
    ok = ok && record.pending && sync.observe(scope, source, remote) == PositionResult::Saved && record.pending &&
         !record.answerable;
  } else if (!std::strcmp(mode, "wake")) {
    ok = ok && record.pending &&
         sync.evaluate(record.token, geometry, "wake-local", decision, applied) == PositionResult::Saved &&
         applied == PositionApply::Ask && sync.answer(record.token, true) == PositionResult::Saved &&
         sync.answer(record.token, true) == PositionResult::Stale && record.marker.raw == remote.raw;
  } else if (!std::strcmp(mode, "override")) {
    auto chosen = remote;
    chosen.at = 5500;
    chosen.percentage = .05;
    chosen.raw = "raw/chosen";
    ok = ok && sync.overrideHere(chosen) == PositionResult::Saved &&
         sync.evaluate(record.token, geometry, "sleep", decision, applied) == PositionResult::Saved &&
         decision.action == PositionAction::Override && sync.mayDeliver(.05, 5500);
  } else if (!std::strcmp(mode, "override-restart")) {
    ok = ok && record.overridePlace.raw == "raw/chosen" &&
         sync.sent(record.overridePlace, true) == PositionResult::Saved && record.lastOverrideAt == 5500 &&
         !sync.mayDeliver(.9, 5400, true);
  } else if (!std::strcmp(mode, "conflict")) {
    ok = ok && store.load({"toy:position", "account:two"}, source, record, scratch, sizeof(scratch)) ==
                   PositionLoad::Conflict;
  } else
    ok = false;
  if (ok)
    LOG_INF("BSP", "BookSync position scenario PASS: draws=%d completions=%d", draws, completions);
  else
    LOG_ERR("BSP", "BookSync position scenario FAIL");
  std::fflush(nullptr);
  std::_Exit(ok ? 0 : 1);
}
