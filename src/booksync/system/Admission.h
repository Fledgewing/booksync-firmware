// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "Memory.h"
#include "TailnetNetPhase.h"
namespace booksync {
// Main-loop-owned policy. Workers receive cancellation through permitted(); they
// never call begin/finish/acquire/reset or touch the renderer. Ports never paint.
struct AdmissionPorts {
  void* context;
  MemorySnapshot (*memory)(void*);
  bool (*parkDisplay)(void*);
  bool (*restoreDisplay)(void*);
  // Must prove transport AND Wi-Fi tasks drained; false retains display ownership.
  bool (*stopNetwork)(void*);
};
enum class AdmissionResult : uint8_t { Admitted, Reading, Busy, Memory, Display, Invalid };
enum class Resource : uint8_t { Tls, Catalog, Cover, Worker };
class Admission {
 public:
  static constexpr size_t minimumFree = 32 * 1024;
  static constexpr size_t minimumLargest = 16 * 1024;
  static constexpr size_t internalFloor = 12 * 1024;
  static constexpr size_t largestFloor = 8 * 1024;
  class Token {
   public:
    Token() = default;
    ~Token() { reset(); }
    Token(const Token&) = delete;
    Token& operator=(const Token&) = delete;
    Token(Token&& other) noexcept;
    Token& operator=(Token&& other) noexcept;
    explicit operator bool() const { return owner != nullptr; }
    void reset();

   private:
    friend class Admission;
    Token(Admission* owner, Resource resource) : owner(owner), resource(resource) {}
    Admission* owner = nullptr;
    Resource resource = Resource::Worker;
  };
  Admission(AdmissionPorts ports, bool needsDisplay) : ports(ports), needsDisplay(needsDisplay) {}
  Admission(const Admission&) = delete;
  Admission& operator=(const Admission&) = delete;
  // Admission outlives all Tokens (the runtime instance has static lifetime).
  void setReading(bool reading);
  AdmissionResult begin(tailnet_service_job_t job);
  Token acquire(Resource resource);
  // Idempotent. Caller first cancels/joins workers, destroys clients and returns
  // every token. Failed teardown/restoration is retried on later main-loop ticks.
  bool finish();
  bool permitted() const { return active.load() && !reading.load() && tailnet_service_permitted(); }
  bool displayReady() const { return !parked.load(); }
  bool busy() const { return owned; }
  bool finishing() const { return draining; }

 private:
  void release(Resource resource);
  AdmissionPorts ports;
  const bool needsDisplay;
  std::atomic<bool> reading{true};
  std::atomic<bool> active{false};
  tailnet_service_lease_t lease{};
  uint8_t resources = 0;
  bool owned = false;
  std::atomic<bool> parked{false};
  bool draining = false;
};
}  // namespace booksync
