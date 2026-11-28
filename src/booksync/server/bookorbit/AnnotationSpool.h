// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AnnotationRecord.h"
#include "booksync/storage/FilePublish.h"

namespace booksync::server {
struct AnnotationSpoolBinding {
  storage::ScopeView scope;
  storage::SourceIdentity source;
  uint64_t entry = 0, nativeOpen = 0;
  std::string_view payload, document;
};
struct AnnotationSpoolState {
  uint64_t round = 0, nativeGeneration = 0;
  uint32_t cursor = 0, nextCursor = 0, rows = 0, keys = 0;
  uint8_t flags = 0;
  bool nextDone = false;
};
class AnnotationRecordReader {
 public:
  virtual ~AnnotationRecordReader() = default;
  virtual const AnnotationRecord* at(size_t) = 0;
};
// Immutable worker-only frozen capture or complete remote reply. Framing is not
// source authority: the owner admits the exact bound source under its closed
// reader/writer lease. Binding views and scratch outlive this object; the input
// stream is immutable across both passes. No payload allocations or open native
// handles survive a call. A damaged current file never falls back to old data.
class AnnotationSpool final : public AnnotationRecordReader {
 public:
  using Admission = bool (*)(void*);
  AnnotationSpool(storage::PublishPaths paths, AnnotationSpoolBinding binding, AnnotationKind kind, bool pending,
                  char* scratch, size_t capacity, Admission admission = nullptr, void* context = nullptr)
      : paths_(paths),
        binding_(binding),
        kind_(kind),
        pending_(pending),
        scratch_(scratch),
        capacity_(capacity),
        admission_(admission),
        context_(context) {}
  AnnotationSpool(const AnnotationSpool&) = delete;
  AnnotationSpool& operator=(const AnnotationSpool&) = delete;
  bool create(const AnnotationSpoolState&, AnnotationRecordReader&, AnnotationKeyReader* = nullptr);
  bool load(AnnotationSpoolState&);
  const AnnotationRecord* at(size_t) override;
  bool key(size_t, AnnotationKey&);

 private:
  storage::PublishPaths paths_;
  AnnotationSpoolBinding binding_;
  AnnotationKind kind_;
  bool pending_;
  char* scratch_;
  size_t capacity_;
  Admission admission_;
  void* context_;
  std::array<char, 136> header_{};
  std::array<char, AnnotationRecordCodec::Header> rowHeader_{};
  AnnotationSpoolState state_{};
  AnnotationRecord decoded_{};
  uint64_t rowsAt_ = 0, keysAt_ = 0, nextAt_ = 0;
  size_t nextIndex_ = 0, keyCacheIndex_ = 0, keyCacheCount_ = 0;
  bool loaded_ = false;
  bool sourceAdmitted() const { return !admission_ || admission_(context_); }
  bool validBinding() const;
  bool validState(const AnnotationSpoolState&) const;
  bool readRecord(FsFile&, AnnotationRecord&);
  bool validate(FsFile&);
  bool emit(AnnotationRecordReader&, AnnotationKeyReader*, AnnotationRecordSink);
  static bool validateFile(FsFile&, void*);
};
}  // namespace booksync::server
