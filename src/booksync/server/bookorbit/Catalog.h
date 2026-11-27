// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/services/CatalogSource.h"
namespace booksync::server {
// Adapter-private authenticated GET seam. open/finish return HTTP status;
// read returns bytes, zero only for completed framing, negative for failure.
// finish must validate Content-Length/chunk terminator and current permissions.
// Implementations stream into the supplied bounded buffer, never buffer a body.
// Credentials/session and base-URL resolution remain private to this transport.
class CatalogTransport {
 public:
  virtual ~CatalogTransport() = default;
  virtual int open(std::string_view relativePath) = 0;
  virtual int read(char*, size_t capacity) = 0;
  virtual int finish() = 0;
  virtual void close() = 0;
};
enum class CatalogFeed : uint8_t { Inventory, Metadata };
struct CatalogRequest {
  std::string_view device;
  std::string_view variant;  // Fixed throughout enumeration; never credentials.
  CatalogFeed feed = CatalogFeed::Inventory;
};
// Pull parser: one normalized row at a time. No JSON tree or page-sized buffer.
// Reusable member scratch (under 5 KiB), no allocations, recursion <= 12 frames.
// Destroy/cancel before releasing transport; one serialized worker owns both.
class Catalog final : public CatalogSource {
 public:
  static constexpr size_t MaxString = 2048, MaxRowBytes = 16384, MaxPageBytes = 4 * 1024 * 1024;
  explicit Catalog(CatalogTransport& transport) : transport_(transport) {}
  ~Catalog() override { cancel(); }
  Catalog(const Catalog&) = delete;
  Catalog& operator=(const Catalog&) = delete;
  bool start(CatalogRequest request);
  CatalogStep next(CatalogRow&) override;
  CatalogError error() const override { return error_; }
  void cancel() override;

 private:
  CatalogTransport& transport_;
  std::array<char, 129> device_{};
  std::array<char, 32> variant_{};
  std::array<char, 1025> cursor_{}, nextCursor_{};
  std::array<char, MaxString + 1> text_{};
  std::array<char, 128> input_{};
  size_t pos_ = 0, available_ = 0, bytes_ = 0, rowStart_ = 0, length_ = 0;
  uint64_t lastId_ = 0;
  uint32_t count_ = 0;
  unsigned pageRows_ = 0, seen_ = 0, page_ = 1;
  uint64_t total_ = 0, expectedTotal_ = 0;
  CatalogFeed feed_ = CatalogFeed::Inventory;
  bool open_ = false, array_ = false, first_ = true, more_ = false, restart_ = false, done_ = false;
  CatalogError error_ = CatalogError::None;
  bool fail(CatalogError error);
  bool status(int code);
  bool openPage();
  int peek();
  int take();
  void space();
  bool expect(char);
  bool string();
  bool number();
  bool literal(const char*);
  bool skip(unsigned depth = 0);
  bool boolean(bool&);
  bool copy(char*, size_t, bool truncate = false, bool* truncated = nullptr);
  bool unsignedNumber(uint64_t&);
  bool percentage(double&);
  bool identifier(char*, size_t, uint64_t* = nullptr);
  bool envelope(bool initial);
  bool row(CatalogRow&);
  bool file(CatalogFile&);
  bool strings(char* base, size_t stride, size_t capacity, uint8_t& count);
  bool files(CatalogRow&);
};
}  // namespace booksync::server
static_assert(sizeof(booksync::server::Catalog) <= 5000);
static_assert(sizeof(booksync::CatalogRow) <= 11000);
