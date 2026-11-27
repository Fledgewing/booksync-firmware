// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "CatalogSource.h"
#include "Delivery.h"
#include "booksync/storage/ProjectionStore.h"
#include "booksync/system/Admission.h"
namespace booksync {
struct CatalogStart {
  bool (*call)(void*) = nullptr;
  void* context = nullptr;
};
struct CatalogRequest {
  CatalogSource* source = nullptr;
  CatalogStart start{};
  CatalogIndexSource::Bind bind = nullptr;  // Optional verified local identity/path binding.
  void* context = nullptr;
  DeliveryTicket owner{};      // Captured at submission with the immutable adapter session.
  storage::ScopeView scope{};  // Stable F08 server/account tokens captured with that session.
};
// Service-side cooperative run, serialized with account transitions, storage and
// ReadModel mutation, with painting excluded. Library polls immutable results
// through F12 Delivery; no consumer references or callbacks are retained here.
// Source/binder contexts outlive the run. start captures only that account's
// adapter cursor and credentials, and is called AFTER F11 admission.
class CatalogJobs {
 public:
  static constexpr uint16_t Batch = 16;
  __attribute__((used)) CatalogJobs(storage::ProjectionStore& store, ReadModel& model, Admission& admission)
      : store_(store), model_(model), admission_(admission) {}
  ~CatalogJobs();
  CatalogJobs(const CatalogJobs&) = delete;
  CatalogJobs& operator=(const CatalogJobs&) = delete;
  // Returns same lease for coalesced requests. Zero means rejection. Retirement
  // of a view's F12 ticket never calls cancel; explicit Stop names this run.
  uint64_t start(CatalogRequest request, DeliveryTicket current);
  bool tick(DeliveryTicket current);
  void cancel(uint64_t run);
  DeliveryResult result(uint64_t run, DeliveryTicket current) const;
  bool busy() const { return running_ || draining_; }
  uint16_t lastWork() const { return lastWork_; }

 private:
  void settle(DeliveryResult result);
  storage::ProjectionStore& store_;
  ReadModel& model_;
  Admission& admission_;
  Admission::Token catalog_, tls_, worker_;
  CatalogRequest request_{};
  CatalogRow row_{};  // Reused bounded workspace; never on task stack/per-row heap.
  storage::IndexRecord record_{};
  DeliveryTicket owner_{};
  inline static uint64_t nextRun_ = 0;  // Serialized process lifetime; Home recreation never reuses leases.
  uint64_t sequence_ = 0;
  DeliveryResult result_ = DeliveryResult::Invalid;
  uint16_t lastWork_ = 0;
  bool running_ = false, draining_ = false;
};
static_assert(sizeof(CatalogJobs) <= 12 * 1024);
}  // namespace booksync
