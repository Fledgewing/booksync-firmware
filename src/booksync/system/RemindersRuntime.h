// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/services/RemindersFacade.h"
namespace booksync {
// Runtime lifetime outlives Home, accounts and reader instances. The later
// clock/presenter seams supply scalars; no timer callback accesses storage.
RemindersFacade& reminderRuntime();
}  // namespace booksync
