#pragma once
#include "FreeRTOS.h"
TaskHandle_t xTaskCreateStatic(void (*)(void*), const char*, uint32_t, void*, int, StackType_t*, StaticTask_t*);
void vTaskDelete(TaskHandle_t);
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t);
uint32_t ulTaskNotifyTake(int, uint32_t);

eTaskState eTaskGetState(TaskHandle_t);
void vTaskSuspend(TaskHandle_t);
