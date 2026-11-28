// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/services/DownloadSource.h"
namespace booksync::server {
// Authenticated adapter-private resolution: revalidate the exact saved manifest
// selection, permissions and canonical edition/revision, with no primary fallback.
// Session credentials and signed/expiring acquisition paths stay in this port.
struct DownloadTarget {
  storage::SourceIdentity identity{};
  std::array<char, 1024> path{};
};
struct DownloadHeaders {
  int status = 0;
  std::array<char, 128> etag{}, contentRange{};
  uint64_t contentLength = 0;
  bool lengthKnown = false;
};
class DownloadTransport {
 public:
  virtual ~DownloadTransport() = default;
  virtual bool resolve(const DownloadSpec&, DownloadTarget&) = 0;
  // Never follow redirects. Set Range and If-Range only when offset is nonzero.
  virtual bool open(const char* relativePath, uint64_t offset, const char* strongEtag, DownloadHeaders&) = 0;
  virtual int read(void*, size_t capacity) = 0;
  virtual bool finish() = 0;
  virtual void close() = 0;
};
class Download final : public DownloadSource {
 public:
  explicit Download(DownloadTransport& transport) : transport_(transport) {}
  ~Download() override { close(); }
  bool open(const DownloadSpec&, uint64_t offset, const char* validator, DownloadReply&) override;
  int read(void*, size_t capacity) override;
  bool finish() override;
  void close() override;

 private:
  DownloadTransport& transport_;
  DownloadTarget target_{};
  DownloadHeaders headers_{};
  uint64_t expected_ = 0, received_ = 0;
  bool opened_ = false, known_ = false, metadataOnly_ = false;
};
}  // namespace booksync::server
