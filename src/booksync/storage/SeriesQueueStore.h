// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "FilePublish.h"
#include "booksync/interface/SeriesDownloads.h"
#include "booksync/services/DownloadSource.h"
namespace booksync::storage {
using SeriesRowState = SeriesItemState;
struct SeriesQueueBook {
  DownloadSpec file{};
  BookKey key{};
  std::array<char, 96> title{}, destination{};
  std::array<char, 64> variantLabel{};
  uint64_t job = 0;
  SeriesRowState state = SeriesRowState::Queued;
  DownloadError error = DownloadError::None;
  bool cancelWanted = false;
  void clear();
};
struct SeriesQueueRecord {
  std::array<SeriesQueueBook, SeriesNextMax> books{};
  SeriesRequest request{};
  uint64_t id = 0, sequence = 0, token = 0;
  uint8_t count = 0;
  void clear();
};
struct SeriesQueueSlot {
  std::array<char, 64> series{};
  uint64_t id = 0;
  uint16_t cancelMask = 0;
  bool terminal = false;
};
struct SeriesQueueIndex {
  std::array<SeriesQueueSlot, 4> slots{};
  uint64_t sequence = 0, counter = 0;
  void clear();
};
// One serial worker owns all paths. Scope tokens and path prefix are copied;
// neither namespace hashing nor numeric slot names convey authorization.
// No implicit repair, eviction, transport, queue execution or payload allocation.
class SeriesQueueStore {
 public:
  SeriesQueueStore(PublishPaths indexPaths, const char* recordPrefix, ScopeView scope);
  bool recover(SeriesQueueIndex& out);
  bool saveIndex(SeriesQueueIndex& index);
  bool load(uint8_t slot, uint64_t id, SeriesQueueRecord& out);
  bool save(uint8_t slot, SeriesQueueRecord& record);
  bool project(uint8_t slot, uint64_t id, SeriesQueueView& out);
  static constexpr size_t IndexBytes = 456, RecordBase = 296, BookBytes = 1057;

 private:
  PublishPaths indexPaths_;
  std::array<char, 64> server_{}, account_{};
  std::array<char, 180> prefix_{};
  std::array<std::array<char, 200>, 3> paths_{};
  uint64_t expectedId_ = 0;
  bool scopeValid_ = false, recordMode_ = false;
  bool setSlot(uint8_t slot);
  PublishPaths recordPaths() const { return {paths_[0].data(), paths_[1].data(), paths_[2].data()}; }
  bool scopeMatches(const DownloadSpec&) const;
  bool validRecord(const SeriesQueueRecord&) const;
  bool validIndex(const SeriesQueueIndex&) const;
  bool loadFile(PublishPaths, SeriesQueueIndex*, SeriesQueueRecord*);
  bool saveFile(PublishPaths, SeriesQueueIndex*, SeriesQueueRecord*);
  bool confirmFile(FilePublish&, PublishPaths, size_t, SeriesQueueIndex*, SeriesQueueRecord*);
  static bool validFile(FsFile&, void*);
};
static_assert(sizeof(SeriesQueueRecord) <= 16 * 1024);
static_assert(sizeof(SeriesQueueStore) <= 1024);
}  // namespace booksync::storage
