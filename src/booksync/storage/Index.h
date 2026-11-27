// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "FilePublish.h"
#include "Identity.h"
#include "booksync/interface/Models.h"
namespace booksync::storage {
struct IndexRecord {
  SourceIdentity source{};  // Unknown until independently bound; never synthesized from a path.
  std::array<char, 1025> path{};
  std::array<char, 96> title{};
  uint32_t page = 0, pages = 0, seconds = 0;
  bool readingKnown = false, statsKnown = false, titleTruncated = false;
};
// Worker-only, externally serialized. Data slots stream records, never a resident
// catalog. The small manifest is atomically published via F06 after data sync.
class Index {
 public:
  static constexpr uint32_t MaxBooks = 10000;
  static constexpr size_t RecordBytes = 1224;
  static constexpr size_t DataHeaderBytes = 40;
  Index(PublishPaths paths, const char* a, const char* b) : paths_(paths), slots_{a, b} {}
  ~Index();
  bool recover();
  bool begin(Provenance provenance);
  bool append(const IndexRecord& record);
  bool commit();
  void cancel();
  bool read(BookKey key, IndexRecord& record);
  bool summary(BookKey key, BookRow& row);
  uint32_t count() const { return count_; }
  bool ready() const { return ready_; }
  Provenance provenance() const { return provenance_; }

 private:
  PublishPaths paths_;
  const char* slots_[2];
  FsFile writing_;
  Provenance provenance_{}, pending_{};
  uint32_t count_ = 0, pendingCount_ = 0;
  uint8_t slot_ = 1, pendingSlot_ = 0;
  bool ready_ = false, active_ = false, failed_ = false;
  // Reused 1224-byte codec scratch avoids large worker stacks and per-row heap churn.
  std::array<uint8_t, RecordBytes> scratch_{};
  static bool validate(FsFile&, void*);
  bool fail(const char*);
};
}  // namespace booksync::storage
