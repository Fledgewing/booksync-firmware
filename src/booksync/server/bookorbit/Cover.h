// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/services/CoverSource.h"
namespace booksync::server {
// Private authenticated exact-artwork resolver. No acquisition URL or wire
// status escapes this adapter. Session/scope capture outlives the cooperative job.
struct CoverTarget {
  CoverSpec selection{};
  std::array<char, 1024> path{};
};
struct CoverHeaders {
  int status = 0;
  uint64_t bytes = 0;
  bool sizeKnown = false, jpeg = false;
};
class CoverTransport {
 public:
  virtual ~CoverTransport() = default;
  virtual bool resolve(const CoverSpec&, CoverTarget&) = 0;
  // Same-origin only; redirects disabled, bounded timeouts, no auth in logs.
  virtual bool open(const char* relativePath, CoverHeaders&) = 0;
  virtual int read(void*, size_t) = 0;
  virtual bool finish() = 0;
  virtual void close() = 0;
};
class Cover final : public CoverSource {
 public:
  explicit Cover(CoverTransport& transport) : transport_(transport) {}
  ~Cover() override { close(); }
  bool open(const CoverSpec&, CoverReply&) override;
  int read(void*, size_t) override;
  bool finish() override;
  void close() override;

 private:
  CoverTransport& transport_;
  CoverTarget target_{};
  CoverHeaders headers_{};
  uint64_t received_ = 0;
  bool opened_ = false;
};
}  // namespace booksync::server
