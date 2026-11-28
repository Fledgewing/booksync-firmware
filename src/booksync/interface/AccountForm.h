// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>

#include "booksync/interface/AccountView.h"
namespace booksync {
enum class AccountScene : uint8_t { Out, In, Limited, Manual, Busy, Phone, Wait, Expired, Failed, TwoStep, Old };
enum class AccountAction : uint8_t { Begin, Phone, Check, Manual, Cancel };
struct AccountFormRequest {
  DeliveryTicket ticket{};
  uint64_t form = 0;
  AccountAction action = AccountAction::Begin;
};
struct AccountFormView {
  AccountView account{};
  DeliveryTicket origin{};
  // Form IDs never repeat; revisions increase for every visible publication.
  uint64_t form = 0, revision = 0;
  AccountScene scene = AccountScene::Out;
  // Service-validated public approval link only; never a session credential.
  std::array<char, 256> link{};
};
// Transient write-only command input. Never part of a display snapshot.
// All paths, including refusal, erase every byte before returning.
struct AccountEntry {
  std::array<char, 513> server{};
  std::array<char, 101> user{};
  std::array<char, 1025> password{};
  void clear() {
    server.fill(0);
    user.fill(0);
    password.fill(0);
  }
};
static_assert(sizeof(AccountEntry) <= 1640);
static_assert(sizeof(AccountFormView) <= 368);
}  // namespace booksync
