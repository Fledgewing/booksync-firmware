// SPDX-License-Identifier: GPL-3.0-or-later
#include <Logging.h>
#include <Memory.h>

#include "AnnotationSpool.h"
#include "ClippingStore.h"

static_assert(sizeof(booksync::server::AnnotationSpool) <= 1536);
// Dormant budget root, never a startup/paint caller. Account/source admission,
// native reader exclusion and the serialized worker remain the runtime owner's
// responsibility. Retain the complete concrete paths in physical size receipts.
extern "C" bool booksync_annotation_application_link(
    booksync::storage::PublishPaths paths, booksync::server::AnnotationSpoolBinding binding,
    booksync::server::AnnotationKind kind, bool pending, const booksync::server::AnnotationSpoolState& state,
    booksync::server::AnnotationRecordReader& rows, booksync::server::AnnotationKeyReader& keys, char* scratch,
    size_t capacity, const std::string& nativePath, const Clipping& clipping, const std::string& passage,
    const std::string& key, bool (*barrier)(const char*)) {
  auto spool = makeUniqueNoThrow<booksync::server::AnnotationSpool>(paths, binding, kind, pending, scratch, capacity);
  if (!spool) {
    LOG_ERR("BSA", "Cannot allocate annotation publication workspace");
    return false;
  }
  booksync::server::AnnotationSpoolState restored;
  booksync::server::AnnotationKey capturedKey;
  const bool created = spool->create(state, rows, &keys);
  const bool loaded = spool->load(restored);
  const bool row = !state.rows || spool->at(0);
  const bool keyed = !state.keys || spool->key(0, capturedKey);
  const bool projected = ClippingStore::applyProjection(nativePath, clipping, passage, key, false, barrier);
  return created && loaded && row && keyed && projected;
}
