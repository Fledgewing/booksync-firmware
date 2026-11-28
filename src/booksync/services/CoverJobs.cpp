// SPDX-License-Identifier: GPL-3.0-or-later
#include "CoverJobs.h"

#include <Logging.h>

#include <cstring>
#include <limits>
namespace booksync {
namespace {
bool text(const std::array<char, 64>& s) { return s[0] && std::memchr(s.data(), 0, s.size()); }
bool versioned(const CoverSpec& s) {
  for (auto b : s.revision)
    if (b) return true;
  return false;
}
bool book(const CoverSpec& a, const CoverSpec& b) {
  return a.server == b.server && a.account == b.account && a.book == b.book;
}
}  // namespace
CoverJobs::~CoverJobs() {
  if (pending_ || running_) settle(false);
}
bool CoverJobs::same(DeliveryTicket t) const {
  return t.owner && t.epoch && t.owner == owner_.owner && t.epoch == owner_.epoch;
}
void CoverJobs::adopt(DeliveryTicket t) {
  if (same(t) || t.epoch <= owner_.epoch) return;
  if (pending_ || running_) obsolete_ = true;
  owner_ = t;
  scoped_ = false;
  for (auto& entry : cache_) {
    entry.id = 0;
    entry.ready = false;
    entry.spec = {};
  }
}
void CoverJobs::observe(const CoverSpec& spec, DeliveryTicket t) {
  if (!same(t)) return;
  for (size_t i = 0; i < Slots; ++i) {
    auto& e = cache_[i];
    if (e.id && book(e.spec, spec) && e.spec.revision != spec.revision) {
      if ((pending_ || running_) && i == slot_) obsolete_ = true;
      e.id = 0;
      e.ready = false;
    }
  }
}
uint64_t CoverJobs::request(const CoverSpec& spec, CoverSource& source, DeliveryTicket t) {
  if (!owner_.owner && !owner_.epoch) adopt(t);
  if (!same(t) || !text(spec.server) || !text(spec.account) || !text(spec.book)) return 0;
  // Binding is fixed for this account epoch, even if every entry was evicted.
  if (scoped_ && (spec.server != server_ || spec.account != account_)) return 0;
  observe(spec, t);
  if (!versioned(spec)) return 0;
  for (const auto& e : cache_)
    if (e.id && e.spec == spec && (e.ready || ((pending_ || running_) && &e == &cache_[slot_]))) return e.id;
  if (busy() || next_ == std::numeric_limits<uint64_t>::max()) return 0;
  slot_ = victim_;
  victim_ = (victim_ + 1) % Slots;
  auto& e = cache_[slot_];
  e.spec = spec;
  e.id = ++next_;
  e.ready = false;
  reply_ = {};
  server_ = spec.server;
  account_ = spec.account;
  scoped_ = true;
  source_ = &source;
  received_ = 0;
  pending_ = true;
  return e.id;
}
void CoverJobs::settle(bool ready) {
  if (source_) source_->close();
  source_ = nullptr;
  if (file_) {
    if (!file_.close()) ready = false;
  }
  cache_[slot_].ready = ready;
  pending_ = false;
  obsolete_ = false;
  if (running_) {
    if (Storage.exists(staging_) && !Storage.remove(staging_)) LOG_ERR("BSC", "Cover scratch retained");
    cover_.reset();
    tls_.reset();
    worker_.reset();
    draining_ = !admission_.finish();
    running_ = false;
  }
}
void CoverJobs::tick(DeliveryTicket t) {
  if (draining_) {
    draining_ = !admission_.finish();
    return;
  }
  if (!pending_ && !running_) return;
  if (obsolete_ || !same(t)) {
    settle(false);
    return;
  }
  auto& entry = cache_[slot_];
  if (pending_) {
    if (admission_.begin(TAILNET_JOB_TRANSFER) != AdmissionResult::Admitted) {
      settle(false);
      return;
    }
    running_ = true;
    pending_ = false;
    cover_ = admission_.acquire(Resource::Cover);
    tls_ = admission_.acquire(Resource::Tls);
    worker_ = admission_.acquire(Resource::Worker);
    if (!cover_ || !tls_ || !worker_ || !admission_.permitted()) {
      settle(false);
      return;
    }
    if (!source_->open(entry.spec, reply_) || reply_.selection != entry.spec ||
        (reply_.sizeKnown && (!reply_.bytes || reply_.bytes > MaxInput)) || !admission_.permitted()) {
      LOG_ERR("BSC", "Cover selection or input length rejected");
      settle(false);
      return;
    }
    file_ = Storage.open(staging_, O_WRONLY | O_CREAT | O_TRUNC);
    if (!file_) {
      LOG_ERR("BSC", "Cover staging open failed");
      settle(false);
    }
    return;
  }
  if (!admission_.permitted()) {
    settle(false);
    return;
  }
  const int n = source_->read(buffer_.data(), buffer_.size());
  if (!admission_.permitted() || n < 0 || n > static_cast<int>(Chunk) || uint32_t(n) > MaxInput - received_ ||
      (reply_.sizeKnown && received_ + n > reply_.bytes)) {
    LOG_ERR("BSC", "Cover body rejected");
    settle(false);
    return;
  }
  if (n) {
    if (file_.write(buffer_.data(), n) != size_t(n)) {
      LOG_ERR("BSC", "Cover staging write failed");
      settle(false);
      return;
    }
    received_ += n;
    return;
  }
  if (!received_ || (reply_.sizeKnown && received_ != reply_.bytes) || !source_->finish() || !admission_.permitted()) {
    LOG_ERR("BSC", "Cover framing rejected");
    settle(false);
    return;
  }
  source_->close();
  source_ = nullptr;
  tls_.reset();
  // Close transport and its writer before the decoder opens one reader. Decode
  // borrows one reused JPEGDEC workspace; no framebuffer/whole image allocation.
  const bool flushed = file_.sync();
  const bool closed = file_.close();
  const bool decoded =
      flushed && closed &&
      decoder_.decode(
          staging_, entry.bitmap, [](void* p) { return static_cast<Admission*>(p)->permitted(); }, &admission_);
  settle(decoded && same(t) && admission_.permitted());
}
DeliveryResult CoverJobs::result(uint64_t id, DeliveryTicket t) const {
  if (!same(t) || !id) return DeliveryResult::Canceled;
  for (const auto& e : cache_)
    if (e.id == id) {
      if (busy() && &e == &cache_[slot_]) return DeliveryResult::Pending;
      return e.ready ? DeliveryResult::Ready : DeliveryResult::Unavailable;
    }
  return DeliveryResult::Canceled;
}
CoverReference CoverJobs::reference(uint64_t id, DeliveryTicket t) const {
  if (result(id, t) != DeliveryResult::Ready) return {};
  for (const auto& e : cache_)
    if (e.id == id) return {id, e.bitmap.width, e.bitmap.height};
  return {};
}
bool CoverJobs::copy(CoverReference ref, DeliveryTicket t, CoverBitmap& out) const {
  if (result(ref.id, t) == DeliveryResult::Ready)
    for (const auto& e : cache_)
      if (e.id == ref.id && ref.width == e.bitmap.width && ref.height == e.bitmap.height) {
        out = e.bitmap;
        return true;
      }
  out = {};
  return false;
}
}  // namespace booksync
extern "C" bool booksync_cover_jobs_link_anchor(booksync::CoverJobs& jobs, const booksync::CoverSpec& spec,
                                                booksync::CoverSource& source, booksync::DeliveryTicket owner,
                                                booksync::CoverBitmap& out) {
  jobs.adopt(owner);
  jobs.observe(spec, owner);
  const auto id = jobs.request(spec, source, owner);
  jobs.tick(owner);
  return jobs.copy(jobs.reference(id, owner), owner, out);
}
