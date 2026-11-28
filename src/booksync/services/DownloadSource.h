// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <HalStorage.h>

#include "booksync/interface/Downloads.h"
#include "booksync/storage/Identity.h"
namespace booksync {
// Worker-only immutable selection. No credentials, URLs or expiring locators.
struct DownloadSpec {
  // A first acquisition has issued book/edition IDs and an unknown (zero)
  // revision. The worker issues SHA-256 evidence only after private proof checks.
  storage::SourceIdentity identity{};
  std::array<char, 64> server{}, account{}, book{}, file{}, variant{};
  // Opaque exact-selection version/proof. Only the adapter interprets these;
  // credentials and acquisition locators must never be put here.
  std::array<char, 64> version{};
  std::array<char, 65> proof{};
  std::array<char, 240> path{};
  uint64_t bytes = 0;
  bool sizeKnown = false;
  bool operator==(const DownloadSpec&) const = default;
};
struct DownloadReply {
  storage::SourceIdentity identity{};
  std::array<char, 128> validator{};  // Opaque strong revision validator; empty disables Range.
  uint64_t offset = 0, bytes = 0;
  bool sizeKnown = false;
};
struct DownloadCheck {
  uint8_t* bytes;
  size_t capacity;
  bool (*permitted)(void*);
  void* context;
};
class DownloadSource {
 public:
  virtual ~DownloadSource() = default;
  // Re-resolve the exact selection with current authorization. A declined range
  // returns offset zero; any other offset is an error. Never choose another file.
  virtual bool open(const DownloadSpec&, uint64_t offset, const char* validator, DownloadReply&) = 0;
  virtual DownloadError error() const { return DownloadError::Unavailable; }
  // For a first acquisition without a known SHA-256, verify the adapter's
  // frozen proof using this already-open handle and borrowed scratch. No network,
  // allocation, second reader or identity issuance is permitted here.
  virtual bool verify(FsFile&, const DownloadSpec&, DownloadCheck) { return false; }
  virtual int read(void*, size_t capacity) = 0;
  // Validate framing and current authorization before publication.
  virtual bool finish() = 0;
  virtual void close() = 0;
};
}  // namespace booksync
