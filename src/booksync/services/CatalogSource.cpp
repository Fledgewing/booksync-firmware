// SPDX-License-Identifier: GPL-3.0-or-later
#include "CatalogSource.h"

#include <Logging.h>
namespace booksync {
ScanStep CatalogIndexSource::next(void* context, storage::IndexRecord& record) {
  auto& self = *static_cast<CatalogIndexSource*>(context);
  const auto step = self.source_.next(self.row_);
  if (step == CatalogStep::End) return ScanStep::End;
  if (step == CatalogStep::Error) return ScanStep::Error;
  record = {};
  if (!self.bind_ || !self.bind_(self.context_, self.row_, record)) {
    LOG_ERR("BSC", "Catalog binding unresolved; generation retained");
    self.source_.cancel();
    return ScanStep::Error;
  }
  return ScanStep::Record;
}
}  // namespace booksync
