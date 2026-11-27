// SPDX-License-Identifier: GPL-3.0-or-later
// Heap-floor policy adapted from BookSync's HeapFloor; internal fragmentation is
// checked separately from total free memory. PSRAM is data storage, never stack.
#include "Memory.h"

#include <Logging.h>

#include <cstdlib>
#include <cstring>
#if defined(ARDUINO_ARCH_ESP32) && FREEINK_FB_PSRAM && defined(FREEINK_NET_WOLFSSL) && !defined(SIMULATOR)
#include <wolfssl/wolfcrypt/memory.h>
#include <wolfssl/wolfcrypt/settings.h>
#define BOOKSYNC_TLS_ALLOCATOR 1
#else
#define BOOKSYNC_TLS_ALLOCATOR 0
#endif
#if defined(ARDUINO_ARCH_ESP32) && !defined(SIMULATOR)
#include <esp_heap_caps.h>
#endif
namespace booksync {
MemorySnapshot memorySnapshot() {
#if defined(ARDUINO_ARCH_ESP32) && !defined(SIMULATOR)
  constexpr auto internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  return {heap_caps_get_free_size(internal), heap_caps_get_largest_free_block(internal),
          heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)};
#else
  // No simulated heap number is presented as an ESP32 measurement.
  return {SIZE_MAX, SIZE_MAX, 0};
#endif
}
void DataDeleter::operator()(uint8_t* pointer) const {
#if defined(ARDUINO_ARCH_ESP32) && !defined(SIMULATOR)
  heap_caps_free(pointer);
#else
  std::free(pointer);
#endif
}
DataBuffer allocateData(size_t bytes) {
  if (!bytes || bytes > maximumDataBytes) {
    LOG_ERR("ADMISSION", "Invalid data allocation: %u bytes", static_cast<unsigned>(bytes));
    return {};
  }
#if defined(ARDUINO_ARCH_ESP32) && !defined(SIMULATOR)
#if FREEINK_FB_PSRAM
  // The DataBuffer deleter owns this bounded, runtime-sized allocation. It must
  // outlive parsing/decode; putting up to 64 KiB on a worker stack is unsafe.
  if (auto* p = static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)))
    return DataBuffer(p);
#endif
  const auto heap = memorySnapshot();
  constexpr size_t floor = 12 * 1024;
  constexpr size_t largestFloor = 8 * 1024;
  if (heap.internalFree < bytes + floor || heap.internalLargest < bytes + largestFloor) {
    LOG_ERR("ADMISSION", "Data deferred: internal free=%u largest=%u need=%u", static_cast<unsigned>(heap.internalFree),
            static_cast<unsigned>(heap.internalLargest), static_cast<unsigned>(bytes));
    return {};
  }
  DataBuffer result(static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
#else
  DataBuffer result(static_cast<uint8_t*>(std::malloc(bytes)));
#endif
  if (!result) LOG_ERR("ADMISSION", "Data allocation failed: %u bytes", static_cast<unsigned>(bytes));
  return result;
}
#if BOOKSYNC_TLS_ALLOCATOR
namespace {
void* tlsAllocate(size_t bytes) {
  if (!bytes) return nullptr;
  // wolfSSL owns these runtime-sized allocations, including protected-book
  // crypto. The job buffer cap must not change reader allocation semantics.
  if (void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) return p;
  const auto heap = memorySnapshot();
  constexpr size_t floor = 12 * 1024;
  constexpr size_t largestFloor = 8 * 1024;
  if (bytes > SIZE_MAX - floor || heap.internalFree < bytes + floor || heap.internalLargest < bytes + largestFloor) {
    LOG_ERR("ADMISSION", "TLS/crypto internal reserve refused %u bytes", static_cast<unsigned>(bytes));
    return nullptr;
  }
  void* result = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!result) LOG_ERR("ADMISSION", "TLS/crypto allocation failed: %u bytes", static_cast<unsigned>(bytes));
  return result;  // wolfSSL frees through tlsFree.
}
void tlsFree(void* pointer) { DataDeleter{}(static_cast<uint8_t*>(pointer)); }
void* tlsReallocate(void* pointer, size_t bytes) {
  if (!bytes) {
    tlsFree(pointer);
    return nullptr;
  }
  DataBuffer replacement(static_cast<uint8_t*>(tlsAllocate(bytes)));
  if (!replacement) return nullptr;  // Existing TLS storage remains owned on failure.
  if (pointer) {
    const size_t oldSize = heap_caps_get_allocated_size(pointer);
    std::memcpy(replacement.get(), pointer, bytes < oldSize ? bytes : oldSize);
    tlsFree(pointer);
  }
  return replacement.release();  // wolfSSL frees through tlsFree.
}
}  // namespace
#endif
bool installNetworkMemory() {
#if BOOKSYNC_TLS_ALLOCATOR
  static bool installed = false;
  if (!installed) {
    if (wolfSSL_SetAllocators(tlsAllocate, tlsFree, tlsReallocate) != 0) {
      LOG_ERR("ADMISSION", "TLS allocator installation failed");
      return false;
    }
    installed = true;
  }
#endif
  return true;
}
void reportMemory(const char* phase, size_t stackHighWaterBytes) {
#ifdef SIMULATOR
  LOG_INF("ADMISSION", "%s: native worker completed; ESP32 heap/stack metrics unavailable", phase);
  (void)stackHighWaterBytes;
#else
  const auto heap = memorySnapshot();
  LOG_INF("ADMISSION", "%s: internal free=%u largest=%u psram free=%u stack never-used=%u bytes", phase,
          static_cast<unsigned>(heap.internalFree), static_cast<unsigned>(heap.internalLargest),
          static_cast<unsigned>(heap.psramFree), static_cast<unsigned>(stackHighWaterBytes));
#endif
}
}  // namespace booksync
