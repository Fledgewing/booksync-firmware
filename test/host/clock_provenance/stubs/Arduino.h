#pragma once
#include <cstdint>
#include <sys/time.h>
inline unsigned long clockElapsed = 0;
inline unsigned long millis() { return clockElapsed; }
inline void delay(unsigned long ms) { clockElapsed += ms; }
