// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include <csetjmp>

#include "booksync/system/NetworkPhase.h"
#include "booksync/system/Worker.h"
#include "freertos/task.h"
bool failWorkerStack = false;
unsigned liveWorkerStacks = 0;
namespace {
unsigned taskCreates = 0;
bool createSuccess = false;
void (*entry)(void*) = nullptr;
eTaskState workerState = eRunning;
std::jmp_buf suspended;
bool joinWhileRunning = false;
TaskHandle_t highWaterTask = nullptr;
booksync::MemorySnapshot snapshot(void*) { return {128 * 1024, 64 * 1024, 0}; }
bool ready(void*) { return true; }
void work(void*) {}
}  // namespace
TaskHandle_t xTaskCreateStatic(void (*function)(void*), const char*, uint32_t bytes, void*, int, StackType_t* stack,
                               StaticTask_t* tcb) {
  EXPECT_EQ(bytes, 6144u);
  EXPECT_NE(stack, nullptr);
  EXPECT_NE(tcb, nullptr);
  ++taskCreates;
  entry = function;
  workerState = eRunning;
  return createSuccess ? reinterpret_cast<TaskHandle_t>(1) : nullptr;
}
void vTaskDelete(TaskHandle_t) { workerState = eDeleted; }
eTaskState eTaskGetState(TaskHandle_t) { return workerState; }
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t task) {
  highWaterTask = task;
  return 2048;
}
void vTaskSuspend(TaskHandle_t) {
  // Completion has been published, but this task is still running here.
  joinWhileRunning = booksync::pollNetworkWorker();
  workerState = eSuspended;
  std::longjmp(suspended, 1);
}
uint32_t ulTaskNotifyTake(int, uint32_t) {
  vTaskSuspend(nullptr);
  return 0;
}
namespace booksync {
Admission& networkAdmission() {
  static Admission a({nullptr, snapshot, ready, ready, ready}, false);
  return a;
}
void reportMemory(const char*, size_t) {}
}  // namespace booksync
TEST(WorkerFault, StackFailureReleasesWorkerPermit) {
  auto& a = booksync::networkAdmission();
  a.setReading(false);
  ASSERT_EQ(a.begin(TAILNET_JOB_TRANSFER), booksync::AdmissionResult::Admitted);
  failWorkerStack = true;
  EXPECT_FALSE(booksync::startNetworkWorker(work, nullptr));
  EXPECT_FALSE(booksync::networkWorkerRunning());
  EXPECT_EQ(liveWorkerStacks, 0u);
  EXPECT_EQ(taskCreates, 0u);
  EXPECT_FALSE(a.busy());
  EXPECT_FALSE(tailnet_service_permitted());
  ASSERT_EQ(a.begin(TAILNET_JOB_TRANSFER), booksync::AdmissionResult::Admitted);
  auto permit = a.acquire(booksync::Resource::Worker);
  EXPECT_TRUE(permit);
  permit.reset();
  EXPECT_TRUE(a.finish());
  failWorkerStack = false;
}
TEST(WorkerFault, TaskCreationFailureFreesInternalStackAndPermit) {
  auto& a = booksync::networkAdmission();
  a.setReading(false);
  ASSERT_EQ(a.begin(TAILNET_JOB_TRANSFER), booksync::AdmissionResult::Admitted);
  EXPECT_FALSE(booksync::startNetworkWorker(work, nullptr));
  EXPECT_FALSE(booksync::networkWorkerRunning());
  EXPECT_EQ(liveWorkerStacks, 0u);
  EXPECT_EQ(taskCreates, 1u);
  EXPECT_FALSE(a.busy());
  EXPECT_FALSE(tailnet_service_permitted());
  ASSERT_EQ(a.begin(TAILNET_JOB_TRANSFER), booksync::AdmissionResult::Admitted);
  auto permit = a.acquire(booksync::Resource::Worker);
  EXPECT_TRUE(permit);
  permit.reset();
  EXPECT_TRUE(a.finish());
}

TEST(WorkerFault, CompletedTaskMustSuspendBeforeStackIsFreed) {
  auto& a = booksync::networkAdmission();
  a.setReading(false);
  ASSERT_EQ(a.begin(TAILNET_JOB_TRANSFER), booksync::AdmissionResult::Admitted);
  createSuccess = true;
  ASSERT_TRUE(booksync::startNetworkWorker(work, nullptr));
  EXPECT_FALSE(booksync::pollNetworkWorker());
  ASSERT_EQ(liveWorkerStacks, 1u);
  // The fake scheduler returns control only after the production entry has
  // published completion and reached its terminal suspension operation.
  if (setjmp(suspended) == 0) entry(nullptr);
  EXPECT_FALSE(joinWhileRunning);
  EXPECT_EQ(liveWorkerStacks, 1u);
  EXPECT_TRUE(booksync::pollNetworkWorker());
  EXPECT_EQ(workerState, eDeleted);
  EXPECT_EQ(highWaterTask, reinterpret_cast<TaskHandle_t>(1));
  EXPECT_EQ(booksync::networkWorkerStackHighWater(), 2048u);
  EXPECT_EQ(liveWorkerStacks, 0u);
  EXPECT_FALSE(booksync::networkWorkerRunning());
  EXPECT_TRUE(a.finish());
  createSuccess = false;
}
