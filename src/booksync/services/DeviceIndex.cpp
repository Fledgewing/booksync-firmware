// SPDX-License-Identifier: GPL-3.0-or-later
#include "DeviceIndex.h"

#include <Logging.h>
namespace booksync {
bool DeviceIndex::start(IndexSource source, Provenance provenance) {
  if (busy_ || !source.next || !index_.begin(provenance)) return false;
  source_ = source;
  busy_ = true;
  return true;
}
bool DeviceIndex::tick() {
  lastWork_ = 0;
  if (!busy_) return true;
  for (; lastWork_ < Batch; ++lastWork_) {
    const auto step = source_.next(source_.context, scratch_);
    if (step == ScanStep::End) {
      busy_ = false;
      if (!index_.commit()) return false;
      model_.invalidate(Invalidation::Generation);
      return true;
    }
    if (step == ScanStep::Error || (step == ScanStep::Record && !index_.append(scratch_))) {
      LOG_ERR("BSI", "Scan stopped; published generation retained");
      cancel();
      return false;
    }
  }
  return true;
}
void DeviceIndex::cancel() {
  index_.cancel();
  busy_ = false;
  source_ = {};
}
}  // namespace booksync
