#pragma once
#include <cstdint>
using StackType_t = uint8_t;
struct StaticTask_t { uint8_t state[64]; };
using TaskHandle_t = void*;
constexpr int pdTRUE = 1;
constexpr uint32_t portMAX_DELAY = UINT32_MAX;

enum eTaskState { eRunning, eBlocked, eSuspended, eDeleted };
