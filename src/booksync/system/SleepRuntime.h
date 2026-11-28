// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace booksync {
enum class SleepReadiness { Draining, Ready, Expired };
// Main-loop bridge. Captures once before cancelling existing worker admission;
// never starts a connection or a new sync job for sleep.
SleepReadiness prepareSleep(uint32_t now);
// Native deep sleep reboots; the simulator and failed storage shutdown return.
// Both must retire exactly this pass before normal foreground work resumes.
void retireSleep();
}  // namespace booksync
