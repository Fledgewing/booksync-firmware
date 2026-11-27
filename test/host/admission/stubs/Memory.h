#pragma once
#include <cstdint>
#include <cstdlib>
#include <memory>
extern bool failWorkerStack;
extern unsigned liveWorkerStacks;
struct StackDeleter {
  void operator()(uint8_t* p) const { if (p) { --liveWorkerStacks; std::free(p); } }
};
using HeapByteBuffer = std::unique_ptr<uint8_t[], StackDeleter>;
inline HeapByteBuffer makeInternalByteBufferNoThrow(size_t bytes) {
  if (failWorkerStack) return {};
  auto* p = static_cast<uint8_t*>(std::malloc(bytes));
  if (p) ++liveWorkerStacks;
  return HeapByteBuffer(p);
}
