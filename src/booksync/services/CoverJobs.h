// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "CoverDecode.h"
#include "CoverSource.h"
#include "Delivery.h"
#include "booksync/system/Admission.h"
namespace booksync {
// Main-loop cooperative service. request/copy/reference are memory-only. tick
// runs AFTER paint, serialized with catalog/download/account work; never paints
// or retains a consumer. F12 retirement leaves acquisition/cache work intact.
// The facade resolves BookKey -> CoverSpec from its current published catalog.
class CoverJobs {
 public:
  static constexpr size_t Slots = 4, Chunk = 1024;
  static constexpr uint32_t MaxInput = CoverDecode::MaxInput;
  __attribute__((used)) CoverJobs(Admission& admission, const char* staging)
      : admission_(admission), staging_(staging) {}
  ~CoverJobs();
  CoverJobs(const CoverJobs&) = delete;
  CoverJobs& operator=(const CoverJobs&) = delete;
  void adopt(DeliveryTicket);
  // Notify on catalog artwork revision changes, including removal (zero
  // revision). Old references become unusable before another fetch is queued.
  void observe(const CoverSpec&, DeliveryTicket);
  uint64_t request(const CoverSpec&, CoverSource&, DeliveryTicket);
  void tick(DeliveryTicket);
  bool busy() const { return pending_ || running_ || draining_; }
  DeliveryResult result(uint64_t, DeliveryTicket) const;
  CoverReference reference(uint64_t, DeliveryTicket) const;
  bool copy(CoverReference, DeliveryTicket, CoverBitmap&) const;

 private:
  struct Entry {
    CoverSpec spec{};
    CoverBitmap bitmap{};
    uint64_t id = 0;
    bool ready = false;
  };
  bool same(DeliveryTicket) const;
  void settle(bool ready);
  Admission& admission_;
  const char* staging_;  // Private exclusive scratch path; owner outlives service.
  CoverDecode decoder_{};
  std::array<Entry, Slots> cache_{};
  std::array<uint8_t, Chunk> buffer_{};
  FsFile file_;
  CoverSource* source_ = nullptr;
  CoverReply reply_{};
  std::array<char, 64> server_{}, account_{};
  DeliveryTicket owner_{};
  Admission::Token cover_, tls_, worker_;
  inline static uint64_t next_ = 0;
  uint32_t received_ = 0;
  size_t slot_ = 0, victim_ = 0;
  bool scoped_ = false, pending_ = false, running_ = false, draining_ = false, obsolete_ = false;
};
// One fallible allocation by the composition root for a Home service lifetime,
// <=40 KiB, replacing stack/static storage and reused for every cover. Includes
// JPEGDEC and four 3 KiB thumbnails; no payload-sized/per-chunk allocations.
static_assert(sizeof(CoverJobs) <= 40 * 1024);
}  // namespace booksync
