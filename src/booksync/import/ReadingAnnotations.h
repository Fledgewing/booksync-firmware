// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "booksync/storage/FilePublish.h"

namespace booksync::import {
// Read-only bounded random access. Caller serializes access and keeps source
// immutable for the whole call. No source reader, native store or writer is run.
struct AnnotationInput {
  bool (*read)(void*, size_t, void*, size_t);
  void* context;
  size_t bytes;
};
enum class AnnotationFormat : uint8_t { Clippings = 1, Bookmarks, Anchors, BookmarkAnchors, Downloads };
enum class AnnotationStatus : uint8_t { Ready, AlreadyPreserved, Unsupported, Corrupt, IoError, Conflict };
struct AnnotationSummary {
  uint32_t records = 0, tombstones = 0, watermark = 0, sourceCrc32 = 0, serverCrc32 = 0;
  uint8_t version = 0;
  bool recoveryRequired = false;
};
// Views refer to raw source offsets, not normalized locators or minted IDs.
// Tombstones retain the exact scope prefix, including unscoped historical IDs.
struct AnnotationSpan {
  size_t offset, bytes;
  bool tombstone;
};
using AnnotationVisitor = bool (*)(void*, const AnnotationSpan&);
// Validates the complete file before calling visitor. Visitors must stage their
// own output until success; failed input/callback never proves a completed import.
AnnotationStatus inspectAnnotations(AnnotationInput, AnnotationFormat, AnnotationSummary&, AnnotationVisitor = nullptr,
                                    void* context = nullptr);

class ReadingAnnotations {
 public:
  // Worker-only, dormant until F29. One distinct, immutable archive per source,
  // backup and revision, under /.booksync/import/annotations/. Existing
  // different/corrupt archives cause Conflict.
  // Provenance is caller-supplied opaque account/epoch + inventory/source evidence;
  // never infer account identity from a numeric ID or a server CRC.
  AnnotationStatus preserve(const char* source, storage::PublishPaths archive, AnnotationFormat,
                            std::string_view provenance);

 private:
  uint8_t copy_[128]{};  // Reused workspace, no payload allocation or large stack.
};
}  // namespace booksync::import
// Unmangled retention entry point for pre-activation full-link size checks.
extern "C" void booksync_reading_annotations_link();
