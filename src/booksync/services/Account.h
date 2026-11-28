// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/Delivery.h"
#include "booksync/services/AuthAdapter.h"

namespace booksync {
struct AccountSnapshot {
  DeliveryTicket ticket{};
  bool signedIn = false;
  bool syncReady = false;
  bool acquisitionReady = false;
  uint32_t accountOwner = 0;
  LoginOrigin syncOrigin = LoginOrigin::None;
  LoginOrigin acquisitionOrigin = LoginOrigin::None;
  uint32_t syncOwner = 0;
  uint32_t acquisitionOwner = 0;
  AuthCapabilities capabilities{};
};
// One instance on the serial worker, never shared with paint/main-loop callers.
// Requests carry the epoch captured when submitted. Publish copies of snapshot()
// through the existing delivery/mailbox seam; workers never present directly.
class Account final {
 public:
  explicit Account(AuthAdapter& adapter) : adapter(adapter) {}
  Account(const Account&) = delete;
  Account& operator=(const Account&) = delete;
  // Worker-private lifetime hook installed only for an imported connector.
  // Durable retirement must succeed before the live slot can be removed.
  using LoginRetirer = bool (*)(LoginKind, void*);
  void setImportedLoginRetirer(LoginKind kind, LoginRetirer callback, void* context) {
    const unsigned slot = kind == LoginKind::Sync ? 0 : 1;
    retirer_[slot] = callback;
    retirement_[slot] = context;
  }
  AccountSnapshot snapshot() const;
  AuthError signIn(CredentialCapture& input, DeliveryTicket expected);
  AuthError restoreLogin(CredentialCapture& input, DeliveryTicket expected, LoginAuthorization authorization = {});
  AuthError signOut(DeliveryTicket expected);
  AuthError removeLogin(LoginKind kind, DeliveryTicket expected);
  AuthError useAccountLogin(LoginKind kind, DeliveryTicket expected);

 private:
  AuthError acceptLogin(CredentialCapture& input, DeliveryTicket expected, bool restore,
                        LoginAuthorization authorization = {});
  bool current(DeliveryTicket expected) const;
  void publishOwner();
  void setConnector(LoginKind kind, uint32_t owner, LoginOrigin origin);
  LoginRetirer retirer_[2]{};
  void* retirement_[2]{};
  AuthAdapter& adapter;
  AccountSnapshot state{};
};
}  // namespace booksync
