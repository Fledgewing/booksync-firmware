// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/storage/Identity.h"
namespace booksync {
// Worker-only immutable selection. No credentials, URLs or expiring locators.
struct DownloadSpec {
  storage::SourceIdentity identity{};
  std::array<char, 64> server{}, account{}, book{}, file{}, variant{};
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
class DownloadSource {
 public:
  virtual ~DownloadSource() = default;
  // Re-resolve the exact selection with current authorization. A declined range
  // returns offset zero; any other offset is an error. Never choose another file.
  virtual bool open(const DownloadSpec&, uint64_t offset, const char* validator, DownloadReply&) = 0;
  virtual int read(void*, size_t capacity) = 0;
  // Validate framing and current authorization before publication.
  virtual bool finish() = 0;
  virtual void close() = 0;
};
}  // namespace booksync
