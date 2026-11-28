// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Index.h"
#include "booksync/interface/Delivery.h"
#include "booksync/services/CatalogSource.h"
namespace booksync::storage {
// Durable F08 scope and process-local presentation identity are independent.
// Stable opaque server/account tokens own files across reboot; tickets only
// admit live work. A cache bucket hash is never identity or authorization.
// Uses F13's checksummed catalog-v2 index and F06 publication protocol.
class ProjectionStore {
 public:
  ProjectionStore(uint32_t owner, uint64_t epoch, ScopeView scope);
  ProjectionStore(const ProjectionStore&) = delete;
  ProjectionStore& operator=(const ProjectionStore&) = delete;
  bool recover();
  bool matches(DeliveryTicket ticket) const;
  bool accepts(ScopeView scope) const;
  bool begin();
  bool append(const CatalogRow& catalog, const IndexRecord& row);
  bool commit();
  void cancel();
  bool read(BookKey key, CatalogRow& row);
  ~ProjectionStore() { cancel(); }
  Index& index() { return index_; }

 private:
  const uint32_t owner_;
  const uint64_t epoch_;
  std::array<char, 129> server_{}, account_{};
  uint64_t namespace_ = 0;
  bool scopeValid_ = false;
  // Stable path storage outlives Index. 7*96 bytes are object storage, never
  // copied onto a worker task stack or allocated per record.
  std::array<std::array<char, 96>, 7> paths_{};
  Index index_;
  FsFile catalog_;
  uint32_t pendingGeneration_ = 0, pendingCount_ = 0;
  bool failed_ = false;
  bool validCatalog();
};
static_assert(sizeof(ProjectionStore) <= 3 * 1024);
}  // namespace booksync::storage
