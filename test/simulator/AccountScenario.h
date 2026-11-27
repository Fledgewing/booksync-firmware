// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>
#include <thread>

#include "booksync/services/Account.h"
#include "booksync/services/Delivery.h"

inline void runAccountScenario() {
  if (!std::getenv("BOOKSYNC_ACCOUNT_SCENARIO")) return;
  using namespace booksync;
  // Fake serial worker/transport: no radio, durable secrets, screen or native writer.
  // Reusable job storage stays outside the worker stack (less than 2 KiB).
  struct FakeAuth final : AuthAdapter {
    char token[32]{};
    unsigned calls = 0;
    AuthError failure = AuthError::None;
    AuthCapabilities authCapabilities() const override { return {true, true, true}; }
    AuthResult authenticate(const CredentialCapture&) override {
      ++calls;
      if (failure != AuthError::None) return {failure};
      for (auto& byte : token) byte = static_cast<char>(65);
      return {AuthError::None, 1};
    }
    AuthResult useAccountLogin(LoginKind) override { return {AuthError::None, 1}; }
    AuthError removeLogin(LoginKind) override { return AuthError::None; }
    void clearAuth() override { eraseSecret(token, sizeof(token)); }
    bool erased() const {
      for (char byte : token)
        if (byte) return false;
      return true;
    }
  } adapter;
  Account account(adapter);
  CredentialCapture input;
  bool ok = true;
  unsigned checks = 0;
  unsigned readyPublications = 0;
  auto check = [&](bool value) {
    ++checks;
    if (!value) LOG_ERR("F15", "Account check failed: %u", checks);
    ok = ok && value;
  };
  std::thread worker([&] {
    auto capture = [&](LoginKind kind) {
      char secret[16];
      for (auto& byte : secret) byte = static_cast<char>(65);
      bool value = input.capture(kind, "origin-a", "synthetic-user", secret, sizeof(secret));
      for (char byte : secret) value = value && byte == 0;
      return value;
    };
    const auto initial = account.snapshot().ticket;
    check(capture(LoginKind::Sync));
    check(account.signIn(input, initial) == AuthError::None && !input.ready());
    check(!account.snapshot().signedIn && account.snapshot().syncReady);
    check(capture(LoginKind::Account));
    check(account.signIn(input, account.snapshot().ticket) == AuthError::None);
    const auto first = account.snapshot().ticket;
    check(first.epoch != initial.epoch && account.snapshot().signedIn);
    adapter.failure = AuthError::Forbidden;
    check(capture(LoginKind::Account));
    check(account.signIn(input, first) == AuthError::Forbidden && !input.ready());
    check(account.signOut(first) == AuthError::None && adapter.erased() && account.snapshot().syncReady);
    check(capture(LoginKind::Account));
    check(account.signIn(input, first) == AuthError::Stale && !input.ready());
    adapter.failure = AuthError::None;
    check(capture(LoginKind::Account));
    check(account.signIn(input, account.snapshot().ticket) == AuthError::None);
    Delivery old(first, 0, DeliveryPolicy::Owner,
                 {[](void* p, DeliveryResult result) {
                    if (result == DeliveryResult::Ready) ++*static_cast<unsigned*>(p);
                  },
                  &readyPublications});
    old.poll(1, DeliveryResult::Ready, account.snapshot().ticket);
    check(readyPublications == 0 && !old.polling() && account.snapshot().ticket.epoch != first.epoch);
    check(account.useAccountLogin(LoginKind::Acquisition, account.snapshot().ticket) == AuthError::None);
    check(account.signOut(account.snapshot().ticket) == AuthError::None && adapter.erased() &&
          !account.snapshot().acquisitionReady && account.snapshot().syncReady);
    check(account.removeLogin(LoginKind::Sync, account.snapshot().ticket) == AuthError::None &&
          !account.snapshot().syncReady);
  });
  worker.join();
  LOG_INF("F15", "BookSync account scenario %s: %u checks; %u ready publications", ok ? "PASS" : "FAIL", checks,
          readyPublications);
  std::fflush(nullptr);
  std::_Exit(ok && checks == 17 ? 0 : 1);
}
