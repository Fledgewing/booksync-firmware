// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "DownloadJobs.h"
namespace booksync {
enum class CopyState : uint8_t { Missing, Current, Old, Ambiguous, Unverifiable };
// Worker-only policy over F08/F18 match evidence. One durable pending book uses
// the F23 journal/worker, never a second writer or UI/network client. Schedule at
// non-reading sync opportunities. The caller retains rejected discoveries;
// an admitted/paused run stays journaled for retry. Ports must prove the target
// document is closed, including beneath Home/panels; missing proof fails closed.
class CopyJobs {
 public:
  CopyJobs(storage::PublishPaths journal, Admission& admission, DownloadPorts ports)
      : jobs_(journal, admission, ports) {}
  static CopyState classify(const DownloadSpec&, const storage::BindingView*, size_t);
  // Main-loop recovery runs under the caller's SD writer/open exclusion, before
  // exposing a queued path to the reader. No open book/job; never activates
  // networking or parks the display to restore an old filename.
  bool recover(storage::ScopeView scope, DeliveryTicket current);
  uint64_t start(const DownloadSpec&, const storage::BindingView*, size_t, DownloadSource&, DeliveryTicket);
  bool resume(uint64_t id, DownloadSource& source, DeliveryTicket current) { return jobs_.resume(id, source, current); }
  void tick(DeliveryTicket current) { jobs_.tick(current); }
  bool busy() const { return jobs_.busy(); }
  DownloadStatus status(DeliveryTicket current) const { return jobs_.status(current); }
  DeliveryResult result(uint64_t id, DeliveryTicket current) const { return jobs_.result(id, current); }

 private:
  DownloadJobs jobs_;
};
static_assert(sizeof(CopyJobs) <= 4096);
}  // namespace booksync
