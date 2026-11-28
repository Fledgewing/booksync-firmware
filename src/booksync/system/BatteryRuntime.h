// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace booksync {
// Main task only, outside paint. Native activity/read controls remain owners.
void workBatteryCapture();
// Called with renderer excluded before native SD shutdown; no extra wake/task.
void captureBatterySleep();
}  // namespace booksync
