// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Delivery.h"
#include "DownloadSource.h"
#include "booksync/interface/Downloads.h"
#include "booksync/storage/FilePublish.h"
#include "booksync/system/Admission.h"
namespace booksync {
// One serialized transfer, shared with the account/catalog owner. Caller owns
// paths and ports through destruction; never run concurrently with painting or
// another SD writer. No task or payload heap allocation is created here.
struct DownloadPorts {
  void* context;
  uint64_t (*freeBytes)(void*);
  // Durable, idempotent identity/path registration. False retains the completed
  // file for explicit link retry without network or another download.
  bool (*link)(void*, const DownloadSpec&);
};
class DownloadJobs {
 public:
  static constexpr uint64_t MaxBytes = 512ull * 1024 * 1024, Headroom = 200000000;
  static constexpr size_t Chunk = 1024;
  DownloadJobs(storage::PublishPaths journal, Admission& admission, DownloadPorts ports);
  ~DownloadJobs();
  DownloadJobs(const DownloadJobs&) = delete;
  DownloadJobs& operator=(const DownloadJobs&) = delete;
  // Boot is read-only and never starts transport or creates an open intent.
  bool recover(storage::ScopeView scope, DeliveryTicket current);
  uint64_t start(const DownloadSpec&, DownloadSource&, DeliveryTicket current);
  bool resume(uint64_t id, DownloadSource&, DeliveryTicket current);
  void cancel(uint64_t id, DeliveryTicket current);
  void tick(DeliveryTicket current);
  DownloadStatus status(DeliveryTicket current) const;
  DeliveryResult result(uint64_t id, DeliveryTicket current) const;
  bool busy() const { return running_ || draining_; }

 private:
  struct Record {
    DownloadSpec spec{};
    uint64_t id = 0;
    std::array<char, 128> validator{};
    uint8_t canceled = 0;
  } record_{};
  storage::PublishPaths journal_;
  Admission& admission_;
  DownloadPorts ports_;
  Admission::Token tls_, worker_, catalog_;
  DownloadSource* source_ = nullptr;
  DeliveryTicket owner_{};
  DownloadStatus status_{};
  std::array<char, 244> staging_{}, backup_{};
  std::array<uint8_t, Chunk> buffer_{};
  DownloadReply reply_{};
  storage::FilePublish artifact_;
  uint64_t flushed_ = 0;
  bool artifactOpen_ = false;
  bool running_ = false, draining_ = false, scoped_ = false;
  static constexpr size_t JournalBytes = 782;
  bool codec(bool encode);
  bool save();
  bool same(DeliveryTicket) const;
  bool admit();
  bool begin(DownloadSource&);
  bool verify(FsFile&);
  bool verifyPath(const char*);
  bool accepts(const DownloadSpec&) const;
  bool setPaths(const DownloadSpec&);
  void settle(DownloadState, DownloadError = DownloadError::None);
  void fail(DownloadError);
  static bool recordValid(FsFile&, void*);
  static bool artifactValid(FsFile&, void*);
};
static_assert(sizeof(DownloadJobs) <= 4096);
}  // namespace booksync
