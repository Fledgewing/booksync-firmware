// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace booksync {
struct AccountSnapshot;
enum class AuthError : uint8_t;
// Worker-private scoped access. UI uses published Library values only.
AccountSnapshot retainedLoginSnapshot(unsigned scope);
AuthError removeRetainedLogin(unsigned scope);
void prepareUpgradeDetection();
void initializeUpgrade();
bool retainedIdentityReady();
bool retainedUpgradeReady();
bool upgradeNoticePending();
bool upgradeRecoveryRequired();
bool dismissUpgradeNotice();
}  // namespace booksync
