// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <unordered_map>

#include "booksync/system/Memory.h"
#include "esp_heap_caps.h"
using namespace booksync;
extern "C" void* __wrap_esp_mbedtls_mem_calloc(size_t, size_t);
extern "C" void tailnet_log_emit(const char*, ...) {}
namespace {
size_t freeInternal, largestInternal;
bool psramOk, internalOk;
unsigned attempts[2], frees;
std::unordered_map<void*, size_t> allocations;
void* (*tlsAllocate)(size_t) = nullptr;
void (*tlsRelease)(void*) = nullptr;
void* (*tlsResize)(void*, size_t) = nullptr;
void reset() {
  freeInternal = 128 * 1024;
  largestInternal = 64 * 1024;
  psramOk = internalOk = true;
  attempts[0] = attempts[1] = frees = 0;
}
class Memory : public testing::Test {
  void SetUp() override { reset(); }
};
}  // namespace
extern "C" size_t heap_caps_get_free_size(unsigned caps) {
  return caps & MALLOC_CAP_SPIRAM ? 1024 * 1024 : freeInternal;
}
extern "C" size_t heap_caps_get_largest_free_block(unsigned) { return largestInternal; }
extern "C" void* heap_caps_malloc(size_t bytes, unsigned caps) {
  const bool external = caps & MALLOC_CAP_SPIRAM;
  ++attempts[external ? 1 : 0];
  if (!(external ? psramOk : internalOk)) return nullptr;
  void* result = std::malloc(bytes);
  if (result) allocations[result] = bytes;
  return result;
}
extern "C" void* heap_caps_calloc(size_t n, size_t bytes, unsigned caps) { return heap_caps_malloc(n * bytes, caps); }
extern "C" void heap_caps_free(void* p) {
  if (p) ++frees;
  allocations.erase(p);
  std::free(p);
}
extern "C" size_t heap_caps_get_allocated_size(void* p) { return allocations.at(p); }
int wolfSSL_SetAllocators(void* (*allocate)(size_t), void (*release)(void*), void* (*resize)(void*, size_t)) {
  tlsAllocate = allocate;
  tlsRelease = release;
  tlsResize = resize;
  return 0;
}
#if FREEINK_FB_PSRAM
TEST_F(Memory, DataPrefersPsramAndReleasesExactlyOnce) {
  {
    auto data = allocateData(4096);
    ASSERT_TRUE(data);
    EXPECT_EQ(attempts[1], 1u);
    EXPECT_EQ(attempts[0], 0u);
  }
  EXPECT_EQ(frees, 1u);
}
TEST_F(Memory, FallbackNeedsLargestInternalBlockAndFloor) {
  psramOk = false;
  largestInternal = 4096;
  EXPECT_FALSE(allocateData(1024));
  EXPECT_EQ(attempts[0], 0u);
  largestInternal = 64 * 1024;
  freeInternal = 1024;
  EXPECT_FALSE(allocateData(1024));
  EXPECT_EQ(attempts[0], 0u);
  freeInternal = 128 * 1024;
  EXPECT_TRUE(allocateData(1024));
  EXPECT_EQ(attempts[0], 1u);
}
TEST_F(Memory, BothAllocationFailuresReturnEmptyWithoutLeaks) {
  psramOk = internalOk = false;
  EXPECT_FALSE(allocateData(4096));
  EXPECT_EQ(attempts[0], 1u);
  EXPECT_EQ(attempts[1], 1u);
  EXPECT_EQ(frees, 0u);
}
TEST_F(Memory, BoundAndOverflowFailBeforeAllocator) {
  EXPECT_FALSE(allocateData(0));
  EXPECT_FALSE(allocateData(maximumDataBytes + 1));
  EXPECT_EQ(__wrap_esp_mbedtls_mem_calloc(SIZE_MAX, 2), nullptr);
  EXPECT_EQ(attempts[0] + attempts[1], 0u);
}
TEST_F(Memory, TlsPsramFailureCannotConsumeInternalReserve) {
  psramOk = false;
  largestInternal = 4096;
  EXPECT_EQ(__wrap_esp_mbedtls_mem_calloc(1, 1024), nullptr);
  EXPECT_EQ(attempts[0], 0u);
  largestInternal = 64 * 1024;
  auto* p = __wrap_esp_mbedtls_mem_calloc(1, 1024);
  EXPECT_NE(p, nullptr);
  heap_caps_free(p);
  EXPECT_EQ(frees, 1u);
}

TEST_F(Memory, SharedCryptoAllocatorPreservesLargeReaderAllocations) {
  ASSERT_TRUE(installNetworkMemory());
  ASSERT_NE(tlsAllocate, nullptr);
  void* p = tlsAllocate(maximumDataBytes + 1);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(attempts[1], 1u);
  EXPECT_EQ(attempts[0], 0u);
  tlsRelease(p);
  EXPECT_EQ(frees, 1u);
  EXPECT_TRUE(allocations.empty());
}
TEST_F(Memory, CryptoResizePreservesOldDataOnFailureAndGrowth) {
  ASSERT_TRUE(installNetworkMemory());
  auto* old = static_cast<unsigned char*>(tlsAllocate(64));
  ASSERT_NE(old, nullptr);
  std::memset(old, 0x5a, 64);
  psramOk = internalOk = false;
  EXPECT_EQ(tlsResize(old, 128), nullptr);
  EXPECT_EQ(frees, 0u);
  for (size_t i = 0; i < 64; ++i) EXPECT_EQ(old[i], 0x5a);
  psramOk = true;
  auto* grown = static_cast<unsigned char*>(tlsResize(old, 128));
  ASSERT_NE(grown, nullptr);
  for (size_t i = 0; i < 64; ++i) EXPECT_EQ(grown[i], 0x5a);
  EXPECT_EQ(frees, 1u);
  tlsRelease(grown);
  EXPECT_EQ(frees, 2u);
  EXPECT_TRUE(allocations.empty());
}

#else
TEST_F(Memory, C3StreamsWithinEightKiBWithoutPsram) {
  EXPECT_FALSE(allocateData(8 * 1024 + 1));
  EXPECT_EQ(attempts[0] + attempts[1], 0u);
  EXPECT_TRUE(allocateData(8 * 1024));
  EXPECT_EQ(attempts[0], 1u);
  EXPECT_EQ(attempts[1], 0u);
  EXPECT_EQ(frees, 1u);
}
TEST_F(Memory, C3LargestBlockAndAllocationFault) {
  largestInternal = 4096;
  EXPECT_FALSE(allocateData(1024));
  EXPECT_EQ(attempts[0], 0u);
  largestInternal = 64 * 1024;
  internalOk = false;
  EXPECT_FALSE(allocateData(1024));
  EXPECT_EQ(attempts[0], 1u);
  EXPECT_EQ(frees, 0u);
}
TEST_F(Memory, C3TlsOverflowAndReserve) {
  EXPECT_EQ(__wrap_esp_mbedtls_mem_calloc(SIZE_MAX, 2), nullptr);
  largestInternal = 4096;
  EXPECT_EQ(__wrap_esp_mbedtls_mem_calloc(1, 1024), nullptr);
  EXPECT_EQ(attempts[0] + attempts[1], 0u);
}
#endif
