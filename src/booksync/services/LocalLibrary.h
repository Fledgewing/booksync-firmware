// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>

#include "booksync/interface/Commands.h"
#include "booksync/interface/Models.h"
#include "booksync/services/widgets/BookList.h"
#include "booksync/services/widgets/Collections.h"
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
// Main-loop composition root borrows frozen F39 values; clear before owner
// release. The existing capture owner remains the only sync writer.
bool bindHomeCollectionSources(const LibraryWidgetInput*, ListsWidgetSource, ReadingScope);
bool takeCatalogRequest();
bool takeCatalogAccountRequest();
bool localLibraryActive();
class ServerAdapter;
void enterLocalLibrary(ServerAdapter* savedStatsSource = nullptr);
// Main-loop composition seam. Borrowed frozen data must outlive Home; clear it
// before owner retirement. Only the existing capture/sync owner registers it.
bool bindHomeBookListSource(BookListSource, ReadingScope, uint16_t year);
void leaveLocalLibrary();
void workLocalLibrary();
void invalidateLocalCapture();
bool takeCatalogBrowseRequest(CatalogRoute& route);
bool takeSourceBrowseRequest(WindowSource& source);
bool takeIndexedOpenRequest(std::string& path);
}  // namespace booksync
