#pragma once
#include "FreeRTOS.h"
inline TaskHandle_t xTaskGetCurrentTaskHandle() { return reinterpret_cast<void*>(1); }
