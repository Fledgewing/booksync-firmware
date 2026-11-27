// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
namespace booksync {
// Single device-lifetime worker slot, owned by the main loop. The callback must
// check networkAdmission().permitted() between bounded operations and close all
// clients/files before returning. It may write flash/NVS: stack is internal.
using NetworkWork = void (*)(void*);
bool startNetworkWorker(NetworkWork work, void* context);
// Releases task/stack/token only after completion; never forcibly kills I/O.
bool pollNetworkWorker();
bool networkWorkerRunning();
size_t networkWorkerStackHighWater();
}  // namespace booksync
