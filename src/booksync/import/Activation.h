// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Credentials.h"
#include "Identity.h"

namespace booksync::legacy {
enum class ActivationResult : uint8_t { Active, Unavailable };
// Serial boot/worker owner only. Immutable snapshots are activated by one BSA1
// record, never by directory presence. Originals are never opened for writing.
// Removal markers permanently retire an imported slot, including when damaged;
// they are not the current account store for subsequent user-created logins.
class Activation final {
 public:
  Activation();
  Activation(const Activation&) = delete;
  Activation& operator=(const Activation&) = delete;
  // Capture before normal native recovery creates its state directory.
  static bool readerStatePresent();
  ActivationResult boot(Credentials& credentials, const import::IdentityAudit& identity, bool priorReaderState = false);
  ImportResult restore(Credentials& credentials, CredentialFamily family, Account& account, size_t index = 0);
  bool hasLogin(CredentialFamily family, size_t index = 0) const;
  bool remove(CredentialFamily family, size_t index = 0);
  bool noticePending() const;
  bool dismissNotice();
  bool identityReady() const { return ready_ && identityReady_; }
  size_t acquisitionCount() const { return ready_ ? record_[56] : 0; }
  bool recoveryRequired() const {
    return !ready_ || stateRecovery_ || record_[6] ||
           (record_[7] != static_cast<unsigned>(import::IdentityStatus::Absent) && !identityReady_);
  }
  bool ready() const { return ready_; }
  static constexpr const char* WifiPath = "/.booksync/wifi.json";

 private:
  struct Retirement {
    Activation* owner;
    CredentialFamily family;
    size_t index;
  };
  Retirement retirements_[10];
  uint8_t record_[64]{};
  bool ready_ = false, identityReady_ = false, stateRecovery_ = false;
  static bool validRecord(FsFile&, void*);
  static bool retire(LoginKind, void*);
  bool removed(CredentialFamily family, size_t index) const;
};
}  // namespace booksync::legacy
