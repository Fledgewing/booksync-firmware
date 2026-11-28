// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/services/DeviceBridge.h"
namespace booksync {
DeviceBridge& deviceRuntime();
// Main task, outside paint: one checked sample per 30 seconds, shared with native capture.
DeviceBattery sampleNativeBattery();
// Main-loop only, renderer excluded. Native navigation remains activity-owned.
bool workDeviceRuntime(DevicePorts navigation);
void retireDeviceRuntime();
}  // namespace booksync
