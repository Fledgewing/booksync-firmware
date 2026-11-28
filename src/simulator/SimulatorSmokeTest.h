#pragma once

#include <cstdint>

#ifdef SIMULATOR
void runSimulatorSmokeTestTick();
// Synthetic F29 boot faults run before native settings/readers can mutate fixtures.
void runSimulatorUpgradeBootScenario();
void prepareSimulatorSmokeState();
// True when a smoke scenario captured this network reboot request instead of letting the simulator relaunch.
bool simulatorInterceptNetworkRestart(uint32_t target, uint32_t payload);
#endif
