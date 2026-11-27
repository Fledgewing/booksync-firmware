#pragma once
#include "../../file_publish/stubs/Logging.h"
// Logging.h includes Arduino.h on firmware. Keep its macro collision visible on the host.
#define sq(x) ((x) * (x))
