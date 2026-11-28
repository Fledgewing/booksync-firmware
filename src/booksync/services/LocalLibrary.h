// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
namespace booksync {
class RemindersFacade;
void bindReminders(RemindersFacade&);
// Composition root publishes small cached values and a private checkpoint port.
// No storage access occurs through Library snapshot/paint.
void publishUpgradeState(bool pending, bool recovery, bool (*dismiss)());
void enterLocalLibrary();
void leaveLocalLibrary();
void workLocalLibrary();
// Pure main-loop values; safe while a network lease owns storage. Never paint.
void workTimeWidgets(bool twelveHour = false);
void invalidateLocalCapture();
bool takeIndexedOpenRequest(std::string& path);
}  // namespace booksync
