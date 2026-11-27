// Task stacks: the measurement (TAILNET_STACK_LOG) and the size table
// (TAILNET_TASK_STACK_WRAP). Switches: TailnetSysSwitches.h; per-env values:
// the switch table in tailnet_build.py.
//
// The framework's tasks are created by prebuilt code with sizes fixed in its
// sdkconfig: esp_timer.c ("esp_timer"), lwIP's sys_arch.c ("tiT"),
// esp_event.c ("sys_evt"), the WiFi adapter esp_adapter.c ("wifi"), and
// Arduino's NetworkEvents.cpp ("arduino_events"). They all call
// xTaskCreatePinnedToCore (xTaskCreate is an inline wrapper around it in
// ESP-IDF 5.5); CrossInk wraps that one symbol and its wrapper asks this table
// for the size by task name. The FreeRTOS timer task ("Tmr Svc") is created static
// with the memory vApplicationGetTimerTaskMemory hands back; the link wraps
// that too.
//
// Proposed sizes (memory research, 2026-11-20), to set once the X3's
// high-water marks are in: esp_timer 4096, tcpip 3072, arduino_events 2560,
// sys_evt 2048, Tmr Svc 1536. The firmware has no FreeRTOS software timer:
// xTimerCreate, xTimerGenericCommand and xTimerPendFunctionCall(FromISR) are
// all dropped by the linker's section GC (firmware.map, "Discarded input
// sections"), so "Tmr Svc" only ever blocks on its empty queue.
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "TailnetSysSwitches.h"

#if TAILNET_TASK_STACK_WRAP || TAILNET_STACK_LOG
// Each task's stack size as created (bytes), by name, for the report.
#define SIZES_MAX 32
static struct {
  char name[configMAX_TASK_NAME_LEN];
  uint32_t requested;  // what the creator asked for
  uint32_t used;       // what the task got
} s_sizes[SIZES_MAX];
static int s_sizes_n;
static portMUX_TYPE s_sizes_lock = portMUX_INITIALIZER_UNLOCKED;

__attribute__((unused)) static void recordSize(const char* name, uint32_t requested, uint32_t used) {
  if (!name) return;
  portENTER_CRITICAL(&s_sizes_lock);  // no logging in here
  int i = 0;
  while (i < s_sizes_n && strncmp(s_sizes[i].name, name, sizeof(s_sizes[i].name)) != 0) i++;
  if (i < SIZES_MAX) {
    strncpy(s_sizes[i].name, name, sizeof(s_sizes[i].name) - 1);
    s_sizes[i].requested = requested;
    s_sizes[i].used = used;
    if (i == s_sizes_n) s_sizes_n++;
  }
  portEXIT_CRITICAL(&s_sizes_lock);
}

__attribute__((unused)) static uint32_t lookupSize(const char* name, uint32_t* requested) {
  uint32_t used = 0;
  portENTER_CRITICAL(&s_sizes_lock);
  for (int i = 0; i < s_sizes_n; i++) {
    if (strncmp(s_sizes[i].name, name, sizeof(s_sizes[i].name)) == 0) {
      used = s_sizes[i].used;
      *requested = s_sizes[i].requested;
      break;
    }
  }
  portEXIT_CRITICAL(&s_sizes_lock);
  return used;
}
#endif

#if TAILNET_TASK_STACK_WRAP

static uint32_t sizeFor(const char* name, uint32_t requested) {
  static const struct {
    const char* name;
    uint32_t bytes;
  } TABLE[] = {
      {"esp_timer", TAILNET_STACK_ESP_TIMER},
      {"tiT", TAILNET_STACK_TCPIP},
      {"arduino_events", TAILNET_STACK_ARDUINO_EVENTS},
      {"sys_evt", TAILNET_STACK_SYS_EVT},
      {"wifi", TAILNET_STACK_WIFI},
  };
  if (!name) return requested;
  for (size_t i = 0; i < sizeof(TABLE) / sizeof(TABLE[0]); i++) {
    if (strcmp(name, TABLE[i].name) == 0) return TABLE[i].bytes >= 1024 ? TABLE[i].bytes : requested;
  }
  return requested;
}

// CrossInk 2.x wraps xTaskCreatePinnedToCore itself (lib/hal/IpcTaskStack.cpp, its IPC
// stack floor on the S3) and a symbol takes one wrapper: its wrapper calls these two
// (CrossInk patch 0020), so both behaviours run.
uint32_t tailnet_task_stack_size(const char* name, uint32_t requested) { return sizeFor(name, requested); }
void tailnet_task_stack_created(const char* name, uint32_t requested, uint32_t used) {
  recordSize(name, requested, used);
}

void __real_vApplicationGetTimerTaskMemory(StaticTask_t** tcb, StackType_t** stack, uint32_t* size);

void __wrap_vApplicationGetTimerTaskMemory(StaticTask_t** tcb, StackType_t** stack, uint32_t* size) {
#if TAILNET_STACK_TMR_SVC >= 1024
  // As port_common.c does, stack first (it grows down, away from the TCB).
  // The timer task is never deleted, so neither block is ever freed.
  StackType_t* s = (StackType_t*)pvPortMalloc(TAILNET_STACK_TMR_SVC);
  StaticTask_t* t = (StaticTask_t*)pvPortMalloc(sizeof(StaticTask_t));
  if (s && t) {
    *stack = s;
    *tcb = t;
    *size = TAILNET_STACK_TMR_SVC;
    recordSize(configTIMER_SERVICE_TASK_NAME, configTIMER_TASK_STACK_DEPTH, TAILNET_STACK_TMR_SVC);
    return;
  }
  vPortFree(s);
  vPortFree(t);
#endif
  __real_vApplicationGetTimerTaskMemory(tcb, stack, size);
  recordSize(configTIMER_SERVICE_TASK_NAME, *size, *size);
}

#endif  // TAILNET_TASK_STACK_WRAP

#if TAILNET_STACK_LOG

void tailnet_stack_report(const char* when) {
  UBaseType_t n = uxTaskGetNumberOfTasks() + 2;  // room for tasks created meanwhile
  TaskStatus_t* tasks = (TaskStatus_t*)malloc(n * sizeof(TaskStatus_t));
  if (!tasks) {
    printf("I tailnet: stacks @ %s: no memory for the report\n", when);
    return;
  }
  n = uxTaskGetSystemState(tasks, n, NULL);
  printf("I tailnet: stacks @ %s: %u tasks, heap free %u, largest %u (name: size bytes, never used)\n", when,
         (unsigned)n, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
         (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  for (UBaseType_t i = 0; i < n; i++) {
    uint32_t requested = 0;
    const uint32_t size = lookupSize(tasks[i].pcTaskName, &requested);
    // ESP-IDF stacks are in bytes (StackType_t is uint8_t).
    if (size && size != requested) {
      printf("I tailnet:   %-16s %5u (was %u), %5u never used\n", tasks[i].pcTaskName, (unsigned)size,
             (unsigned)requested, (unsigned)tasks[i].usStackHighWaterMark);
    } else if (size) {
      printf("I tailnet:   %-16s %5u, %5u never used\n", tasks[i].pcTaskName, (unsigned)size,
             (unsigned)tasks[i].usStackHighWaterMark);
    } else {
      printf("I tailnet:   %-16s     ?, %5u never used\n", tasks[i].pcTaskName,
             (unsigned)tasks[i].usStackHighWaterMark);
    }
  }
  free(tasks);
  size_t live, peak, total;
  if (tailnet_wifi_alloc_stats(&live, &peak, &total)) {
    printf("I tailnet: WiFi driver heap @ %s: %u held, peak %u, %u allocated in all\n", when, (unsigned)live,
           (unsigned)peak, (unsigned)total);
  }
}

#else

void tailnet_stack_report(const char* when) { (void)when; }

#endif  // TAILNET_STACK_LOG

// "before WiFi off": every path that turns WiFi off (WiFi.mode(WIFI_OFF),
// WiFi.disconnect(true), the restart's shutdown handler) stops the driver here,
// on the caller's task. MicroLink stops first, so none of its tasks outlives the
// driver (Tailnet.h); then the stack report (TAILNET_STACK_LOG).
#include <esp_wifi.h>
#include "Tailnet.h"
#define TAILNET_STOP_BEFORE_WIFI_OFF_MS 5000
esp_err_t __real_esp_wifi_stop(void);
esp_err_t __wrap_esp_wifi_stop(void) {
  tailnet_stop_before_wifi_off(TAILNET_STOP_BEFORE_WIFI_OFF_MS);
  tailnet_stack_report("WiFi off");
  return __real_esp_wifi_stop();
}
