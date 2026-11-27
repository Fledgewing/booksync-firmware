#pragma once

#include <cstdint>

#ifdef SIMULATOR
void runSimulatorSmokeTestTick();
void prepareSimulatorSmokeState();
// Read-only import probes run before native settings/session boot loaders.
void prepareSimulatorImportState();
// True when a smoke scenario captured this network reboot request instead of letting the simulator relaunch.
bool simulatorInterceptNetworkRestart(uint32_t target, uint32_t payload);
#endif
