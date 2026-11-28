// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "booksync/services/Account.h"
#include "booksync/services/LocalLibrary.h"
#include "booksync/services/OfflineLibrary.h"
#include "booksync/services/SyncOwner.h"
#include "booksync/system/Upgrade.h"

inline void runActivationScenario() {
  const char* mode = std::getenv("BOOKSYNC_ACTIVATION_SCENARIO");
  if (!mode) return;
  using namespace booksync;
  unsigned checks = 0;
  bool ok = true;
  auto check = [&](bool value) {
    ++checks;
    if (!value) LOG_ERR("BSA", "Activation check failed: %u", checks);
    ok = value && ok;
  };
  const bool empty = std::strcmp(mode, "signed-out") == 0 || std::strcmp(mode, "fresh") == 0;
  const bool fresh = std::strcmp(mode, "fresh") == 0;
  const bool unavailable = std::strcmp(mode, "unavailable") == 0;
  const bool stateUnavailable = std::strcmp(mode, "state-unavailable") == 0;
  const bool identityBlocked = std::strcmp(mode, "identity-blocked") == 0;
  const auto before = offlineLibrary().snapshot();
  check(before.upgradeRecoveryRequired == (unavailable || identityBlocked || stateUnavailable));
  if (unavailable) {
    check(!retainedIdentityReady());
    check(!retainedLoginSnapshot(0).syncReady);
  } else {
    check(retainedIdentityReady() == (!identityBlocked && !empty));
    for (unsigned scope = 0; scope < 4; ++scope) {
      const bool removed = std::getenv("BOOKSYNC_ACTIVATION_REMOVED") && scope == 0;
      const auto account = retainedLoginSnapshot(scope);
      check((scope < 2 ? account.syncReady : account.acquisitionReady) == (!removed && !empty));
      check(!account.signedIn);  // Legacy sync login is never a web session.
    }
    if (std::strcmp(mode, "remove") == 0) {
      check(removeRetainedLogin(0) == AuthError::None);
      check(!retainedLoginSnapshot(0).syncReady);
      check(retainedLoginSnapshot(1).syncReady && retainedLoginSnapshot(2).acquisitionReady);
    }
    if (std::strcmp(mode, "dismiss") == 0) {
      // The actual Library intent is dispatched outside paint by the worker.
      check(offlineLibrary().submit(Intent::DismissUpgradeNotice) == CommandStatus::Accepted);
      workLocalLibrary();
      check(!offlineLibrary().snapshot().upgradeNoticePending);
    } else {
      const bool dismissed = std::getenv("BOOKSYNC_ACTIVATION_DISMISSED");
      check(before.upgradeNoticePending == (!dismissed && !fresh));
    }
    // No legacy queue is replayed. Only the F17 owner can mint a writer token;
    // a second owner is rejected even with another journal instance.
    struct Toy final : ServerAdapter {
      unsigned requests = 0;
      Capabilities capabilities() const override { return {}; }
      Snapshot cachedSnapshot() const override { return {}; }
      CommandStatus deliver(const SyncWriterToken&, Intent) override {
        ++requests;
        return CommandStatus::Unavailable;
      }
    } toy;
    storage::LifecycleJournal journal(
        {"/.booksync/import/test-journal", "/.booksync/import/test-journal.tmp", "/.booksync/import/test-journal.bak"});
    static char scratch[SyncOwner::ScratchBytes];  // Harness only, not firmware RAM.
    SyncOwner owner(journal, toy, "toy:retained", "scope:synthetic", scratch, sizeof(scratch));
    storage::LifecycleJournal otherJournal(
        {"/.booksync/import/test-second", "/.booksync/import/test-second.tmp", "/.booksync/import/test-second.bak"});
    SyncOwner second(otherJournal, toy, "toy:retained", "scope:synthetic", scratch, sizeof(scratch));
    check(owner.activate() == SyncResult::Ready);
    check(second.activate() == SyncResult::Busy);
    check(owner.step() == SyncResult::Idle);
    check(toy.requests == 0);
    check(owner.deactivate() == SyncResult::Ready);
  }
  LOG_INF("BSA", "BookSync activation scenario %s: checks=%u checkpoints=%u network=0", ok ? "PASS" : "FAIL", checks,
          storage::publicationCheckpoints());
  std::fflush(nullptr);
  std::_Exit(ok ? 0 : 1);
}
