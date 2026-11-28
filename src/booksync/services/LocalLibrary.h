// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
namespace booksync {
class RemindersFacade;
struct ReminderActivity;
void bindReminderActivity(void (*)(ReminderActivity));
class DeviceBridge;
void bindDeviceBridge(DeviceBridge*);
bool deviceStatusWanted();
void notifyDevicePublication();
void bindReminders(RemindersFacade&);
void bindReminderConnect(void (*)());
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
// Pure main-loop values; safe while a network lease owns storage. Never paint.
void workTimeWidgets(bool twelveHour = false);
void invalidateLocalCapture();
bool takeIndexedOpenRequest(std::string& path);
}  // namespace booksync
