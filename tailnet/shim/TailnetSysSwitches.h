// System-level memory switches (task stacks, the tailnet worker, WiFi init,
// WiFi join). Per-env values come from the switch table in tailnet_build.py,
// which defines each one for the whole build (CrossInk too); the defaults
// below only apply to a build that does not run that script's table.
#pragma once

#include <stdbool.h>
#include <stddef.h>

// Task stack report: every task's stack high-water mark at fixed points
// (WiFi joined, Tailscale ready, first HTTPS request done, WiFi off).
#ifndef TAILNET_STACK_LOG
#define TAILNET_STACK_LOG 0
#endif

// Links __wrap_xTaskCreatePinnedToCore and __wrap_vApplicationGetTimerTaskMemory
// (TailnetStacks.c). With every size at 0 it changes nothing; it only
// records each task's stack size for the report.
#ifndef TAILNET_TASK_STACK_WRAP
#define TAILNET_TASK_STACK_WRAP 0
#endif

// Stack size in bytes for the framework's tasks; 0 keeps the size the
// framework asks for. Values below 1024 are ignored.
#ifndef TAILNET_STACK_ESP_TIMER
#define TAILNET_STACK_ESP_TIMER 0  // "esp_timer", CONFIG_ESP_TIMER_TASK_STACK_SIZE 4096
#endif
#ifndef TAILNET_STACK_TCPIP
#define TAILNET_STACK_TCPIP 0  // "tiT", CONFIG_LWIP_TCPIP_TASK_STACK_SIZE 4096
#endif
#ifndef TAILNET_STACK_ARDUINO_EVENTS
#define TAILNET_STACK_ARDUINO_EVENTS 0  // "arduino_events", ARDUINO_NETWORK_EVENT_TASK_STACK_SIZE 4096
#endif
#ifndef TAILNET_STACK_SYS_EVT
#define TAILNET_STACK_SYS_EVT 0  // "sys_evt", CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE 2048
#endif
#ifndef TAILNET_STACK_WIFI
#define TAILNET_STACK_WIFI 0  // "wifi", the driver's own (esp_adapter.c)
#endif
#ifndef TAILNET_STACK_TMR_SVC
#define TAILNET_STACK_TMR_SVC 0  // "Tmr Svc", CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH 2560
#endif

// The tailnet worker task exists only while it has work: created by the first
// request that needs it (WiFi up and Tailscale on; on the C3 also a boot that
// runs Tailscale), and gone once MicroLink is stopped and freed.
#ifndef TAILNET_LAZY_WORKER
#define TAILNET_LAZY_WORKER 1
#endif
#ifndef TAILNET_WORKER_STACK
#define TAILNET_WORKER_STACK 6144
#endif

// WiFi init (TailnetWifiMem.c).
#ifndef TAILNET_WIFI_RX_MGMT
#define TAILNET_WIFI_RX_MGMT 0  // RX management buffers (0 = keep the framework's 5)
#endif
#ifndef TAILNET_WIFI_NO_NVS
#define TAILNET_WIFI_NO_NVS 0  // driver keeps no WiFi config in NVS
#endif
#ifndef TAILNET_WIFI_ALLOC_COUNT
#define TAILNET_WIFI_ALLOC_COUNT 0  // count the WiFi driver's heap use (measurement)
#endif

// Tailscale's low-memory stop (Tailnet.cpp, lite builds): free internal RAM must stay
// below 8 KB this long (ms, sampled every second) before Tailscale is stopped. 0 = at
// once, the first sample below (the old behaviour).
#ifndef TAILNET_LOW_MEMORY_HOLD_MS
#define TAILNET_LOW_MEMORY_HOLD_MS 0
#endif

// CrossInk patch 0005: join the last-used channel and BSSID first, then fall
// back to the full scan.
#ifndef CROSSINK_TAILNET_WIFI_FAST_JOIN
#define CROSSINK_TAILNET_WIFI_FAST_JOIN 0
#endif

// Cached reconnect may use an explicitly released display lease; never reboots.
#ifndef TAILNET_PARK_DURING_CONNECT
#define TAILNET_PARK_DURING_CONNECT 0
#endif

#ifdef __cplusplus
extern "C" {
#endif
// Prints every task's stack high-water mark to the console, tagged `when`.
// No-op unless TAILNET_STACK_LOG. Never call it inside a critical section.
void tailnet_stack_report(const char* when);
// WiFi driver heap (TAILNET_WIFI_ALLOC_COUNT): bytes held now, the peak, and
// the bytes ever allocated. False when not counting.
bool tailnet_wifi_alloc_stats(size_t* live, size_t* peak, size_t* total);
#ifdef __cplusplus
}
#endif
