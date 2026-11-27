// SPDX-License-Identifier: GPL-3.0-or-later
#include "CatalogJobs.h"

#include <Logging.h>

#include <limits>
namespace booksync {
CatalogJobs::~CatalogJobs() {
  if (running_) settle(DeliveryResult::Canceled);
  // Runtime must drain network before destroying the owning service. Admission
  // retains its lease if stop/restore fails; destructor never relinquishes it.
  if (draining_ && !admission_.finish()) LOG_ERR("BSC", "Catalog owner destroyed before network drain");
}
uint64_t CatalogJobs::start(CatalogRequest request, DeliveryTicket current) {
  if (!store_.matches(current) || !store_.accepts(request.scope) || request.owner.owner != current.owner ||
      request.owner.epoch != current.epoch)
    return 0;
  if (running_) return sequence_;  // Single scope/run; no extra transport or parse.
  if (draining_ || !request.source || !request.start.call || nextRun_ == std::numeric_limits<uint64_t>::max()) return 0;
  const auto admitted = admission_.begin(TAILNET_JOB_CATALOG);
  if (admitted != AdmissionResult::Admitted) {
    // begin can fail while its own teardown is still pending. Never finish a
    // Busy lease belonging to a different service.
    draining_ = admitted != AdmissionResult::Busy && admission_.busy();
    return 0;
  }
  catalog_ = admission_.acquire(Resource::Catalog);
  tls_ = admission_.acquire(Resource::Tls);
  worker_ = admission_.acquire(Resource::Worker);
  owner_ = current;
  request_ = request;
  sequence_ = ++nextRun_;
  running_ = true;
  result_ = DeliveryResult::Pending;
  if (!catalog_ || !tls_ || !worker_ || !store_.begin() || !request.start.call(request.start.context)) {
    LOG_ERR("BSC", "Catalog start failed; prior generation retained");
    settle(DeliveryResult::Unavailable);
    return 0;
  }
  return sequence_;
}
void CatalogJobs::settle(DeliveryResult result) {
  // Close transport before returning memory/display ownership. cancel is also
  // the source's idempotent close after successful End; it never paints.
  if (request_.source) request_.source->cancel();
  store_.cancel();
  request_ = {};
  worker_.reset();
  tls_.reset();
  catalog_.reset();
  result_ = result;
  running_ = false;
  draining_ = !admission_.finish();
}
void CatalogJobs::cancel(uint64_t run) {
  if (running_ && run == sequence_) settle(DeliveryResult::Canceled);
}
DeliveryResult CatalogJobs::result(uint64_t run, DeliveryTicket current) const {
  if (!run || run != sequence_ || current.owner != owner_.owner || current.epoch != owner_.epoch)
    return DeliveryResult::Canceled;
  return draining_ ? DeliveryResult::Pending : result_;
}
bool CatalogJobs::tick(DeliveryTicket current) {
  lastWork_ = 0;
  const bool sameAccount = store_.matches(current);
  if (!sameAccount && model_.availability() != Availability::NotSaved) model_.invalidate(Invalidation::Account);
  if (draining_) {
    draining_ = !admission_.finish();
    return sameAccount && !draining_;
  }
  if (!sameAccount) {
    if (running_) settle(DeliveryResult::Canceled);
    return false;
  }
  if (!running_) return true;
  if (!admission_.permitted()) {
    settle(DeliveryResult::Canceled);
    return false;
  }
  for (; lastWork_ < Batch; ++lastWork_) {
    const auto step = request_.source->next(row_);
    if (!admission_.permitted()) {
      settle(DeliveryResult::Canceled);
      return false;
    }
    if (step == CatalogStep::End) {
      const auto before = store_.index().provenance();
      const bool committed = store_.commit();
      // F06 may expose a complete generation despite reporting a failed final
      // directory barrier. Invalidate if reconciliation selected that generation.
      if (store_.index().provenance() != before) model_.invalidate(Invalidation::Generation);
      settle(committed ? DeliveryResult::Ready : DeliveryResult::Unavailable);
      return committed;
    }
    record_ = {};
    record_.title = row_.title;
    record_.titleTruncated = row_.titleTruncated;
    // Server-only rows retain opaque aliases without inventing identity/path.
    // A binder may add independently verified local evidence; a failed claimed
    // binding invalidates the generation rather than silently omitting a row.
    if (step == CatalogStep::Error || (request_.bind && !request_.bind(request_.context, row_, record_)) ||
        !store_.append(row_, record_)) {
      LOG_ERR("BSC", "Catalog interrupted; prior generation retained");
      settle(DeliveryResult::Unavailable);
      return false;
    }
  }
  return true;
}
}  // namespace booksync

// Physical-size root only. Linking creates no instance, opens no file and starts
// no transport. Borrow workspaces so even this dormant root adds no allocation.
extern "C" bool booksync_catalog_jobs_link_anchor(booksync::CatalogJobs& jobs,
                                                  booksync::storage::ProjectionStore& store,
                                                  booksync::CatalogRequest request, booksync::DeliveryTicket owner,
                                                  booksync::CatalogRow& row) {
  const auto run = jobs.start(request, owner);
  const bool ticked = jobs.tick(owner);
  const bool read = store.read({store.index().provenance(), 0}, row);
  const auto result = jobs.result(run, owner);
  jobs.cancel(run);
  return ticked && read && result == booksync::DeliveryResult::Ready;
}
