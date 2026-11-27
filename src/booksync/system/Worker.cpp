// SPDX-License-Identifier: GPL-3.0-or-later
#include "Worker.h"

#include <Logging.h>
#include <Memory.h>

#include <atomic>

#include "NetworkPhase.h"
#ifdef SIMULATOR
#include <thread>
#else
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
namespace booksync {
namespace {
constexpr size_t stackBytes = 6144;
Admission::Token token;
NetworkWork work = nullptr;
void* context = nullptr;
std::atomic<bool> complete{false};
std::atomic<size_t> highWater{0};
#ifdef SIMULATOR
std::thread thread;
#else
// TCB is small permanent control state in internal .bss. The fallible stack is
// held only for this worker's lifetime, not a permanent 6 KiB reservation.
StaticTask_t taskStorage;
TaskHandle_t task = nullptr;
HeapByteBuffer stack;
#endif
void run(void*) {
  work(context);
  complete.store(true);
#ifndef SIMULATOR
  // Completion is not a join: on S3 the main core may see it before this core
  // stops using its stack. The owner waits for terminal suspension before delete.
  for (;;) vTaskSuspend(nullptr);
#endif
}
}  // namespace
bool startNetworkWorker(NetworkWork function, void* parameter) {
  if (!function || token) return false;
  auto permit = networkAdmission().acquire(Resource::Worker);
  if (!permit) {
    LOG_ERR("ADMISSION", "Worker admission refused");
    return false;
  }
#ifndef SIMULATOR
  // Up to 6144 bytes, runtime task lifetime: too large for caller stack; PSRAM
  // is forbidden here even on S3 because callback may write flash or NVS (#183).
  stack = makeInternalByteBufferNoThrow(stackBytes);
  if (!stack) {
    LOG_ERR("ADMISSION", "Internal worker stack allocation failed");
    permit.reset();
    networkAdmission().finish();
    return false;
  }
#endif
  work = function;
  context = parameter;
  complete.store(false);
  token = std::move(permit);
#ifdef SIMULATOR
  thread = std::thread(run, nullptr);
#else
  task = xTaskCreateStatic(run, "booksync-worker", stackBytes, nullptr, 1, reinterpret_cast<StackType_t*>(stack.get()),
                           &taskStorage);
  if (!task) {
    LOG_ERR("ADMISSION", "Worker task creation failed");
    stack.reset();
    token.reset();
    work = nullptr;
    context = nullptr;
    networkAdmission().finish();
    return false;
  }
#endif
  return true;
}
bool pollNetworkWorker() {
  if (!token) return true;
  if (!complete.load()) return false;
#ifdef SIMULATOR
  thread.join();
  highWater = 0;  // Native OS stack is not an ESP32 stack measurement.
#else
  // A statically allocated stack remains caller-owned. Delete only a proven
  // non-running task; vTaskDelete of a task on the other core may defer cleanup.
  if (eTaskGetState(task) != eSuspended) return false;
  highWater = uxTaskGetStackHighWaterMark(task);
  vTaskDelete(task);
  task = nullptr;
  stack.reset();
#endif
  token.reset();
  work = nullptr;
  context = nullptr;
  reportMemory("worker joined", highWater.load());
  return true;
}
bool networkWorkerRunning() { return static_cast<bool>(token); }
size_t networkWorkerStackHighWater() { return highWater.load(); }
}  // namespace booksync
