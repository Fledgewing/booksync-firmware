// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/services/DeviceBridge.h"
namespace booksync {
DeviceBridge& deviceRuntime();
// Main-loop only, renderer excluded. Native navigation remains activity-owned.
bool workDeviceRuntime(DevicePorts navigation);
void retireDeviceRuntime();
}  // namespace booksync
