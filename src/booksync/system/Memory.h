// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
namespace booksync {
struct MemorySnapshot {
  size_t internalFree;
  size_t internalLargest;
  size_t psramFree;
};
MemorySnapshot memorySnapshot();
// Boot-only, before the first TLS connection. PSRAM-capable S3 wolfSSL hook.
bool installNetworkMemory();
// Data only: never use this allocator for stacks, DMA buffers or ISR data.
// Runtime-sized bounded job buffers cannot live on the small task stack.
struct DataDeleter {
  void operator()(uint8_t* pointer) const;
};
using DataBuffer = std::unique_ptr<uint8_t[], DataDeleter>;
#if FREEINK_FB_PSRAM
constexpr size_t maximumDataBytes = 64 * 1024;
#else
constexpr size_t maximumDataBytes = 8 * 1024;
#endif
DataBuffer allocateData(size_t bytes);
void reportMemory(const char* phase, size_t stackHighWaterBytes = 0);
}  // namespace booksync
