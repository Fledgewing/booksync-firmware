// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>

#include "booksync/interface/Commands.h"
#include "booksync/interface/Delivery.h"
namespace booksync {
// Bounded public values. No address, user identity, credential or raw response.
struct AccountView {
  DeliveryTicket ticket{};
  std::array<char, 32> serviceLabel{};
  uint8_t service = 0, selectedService = 0;
  bool available = false, signedIn = false;
  bool accountSupported = false, syncSupported = false, catalogSupported = false;
  bool syncReady = false, catalogReady = false;
};
struct ServiceSelection {
  DeliveryTicket ticket{};
  uint8_t service = 0;  // Zero means local saved books; keys are opaque.
};
static_assert(sizeof(AccountView) <= 64);
}  // namespace booksync
