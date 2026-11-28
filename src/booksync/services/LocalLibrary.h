// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
namespace booksync {
class RemindersFacade;
void bindReminders(RemindersFacade&);
// Composition root publishes small cached values and a private checkpoint port.
// No storage access occurs through Library snapshot/paint.
void publishUpgradeState(bool pending, bool recovery, bool (*dismiss)());
bool takeCatalogRequest();
bool takeCatalogAccountRequest();
bool localLibraryActive();
class ServerAdapter;
void enterLocalLibrary(ServerAdapter* savedStatsSource = nullptr);
void leaveLocalLibrary();
void workLocalLibrary();
void invalidateLocalCapture();
bool takeIndexedOpenRequest(std::string& path);
}  // namespace booksync
