// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "booksync/server/bookorbit/Catalog.h"
#include "booksync/services/CatalogSource.h"
using namespace booksync;
struct Reply final : server::CatalogTransport {
  std::string body, request;
  size_t pos = 0, chunk = 1, cut = SIZE_MAX;
  int code = 200, terminal = 200;
  unsigned closes = 0;
  int open(std::string_view path) override {
    request = path;
    pos = 0;
    return code;
  }
  int read(char* out, size_t n) override {
    if (pos >= cut) return -1;
    n = std::min({n, chunk, body.size() - pos, cut - pos});
    memcpy(out, body.data() + pos, n);
    pos += n;
    return n;
  }
  int finish() override { return terminal; }
  void close() override { ++closes; }
};
static std::string page(std::string row) {
  return "{\"items\":[" + row + "],\"hasNext\":false,\"nextCursor\":null,\"restartRequired\":false}";
}
TEST(Catalog, NormalizesManifestWithoutInventingIdentity) {
  Reply reply;
  reply.body = page(
      R"({"id":7,"title":"A \uD83D\uDE80","authors":["Synthetic author"],"seriesName":"Sequence","seriesIndex":1.5,"formats":["epub"],"files":[{"id":9,"format":"epub","sizeBytes":123,"contentVersion":"revision-a","fileHash":"0123456789abcdef0123456789abcdef","devicePath":"books/a.epub","downloadUrl":"/koreader/plugin/catalog/files/9/download"}]})");
  server::Catalog catalog(reply);
  ASSERT_TRUE(catalog.start({}));
  static CatalogRow row;
  ASSERT_EQ(catalog.next(row), CatalogStep::Record);
  EXPECT_STREQ(row.alias.data(), "7");
  EXPECT_STREQ(row.title.data(), "A 🚀");
  EXPECT_EQ(row.authorCount, 1);
  EXPECT_EQ(row.fileCount, 1);
  EXPECT_STREQ(row.files[0].alias.data(), "9");
  EXPECT_EQ(row.files[0].bytes, 123);
  EXPECT_EQ(catalog.next(row), CatalogStep::End);
  EXPECT_EQ(reply.request, "/koreader/plugin/catalog/manifest?size=200");
  EXPECT_EQ(reply.closes, 1u);
}
TEST(Catalog, RejectsMalformedAndOversized) {
  for (const auto& body :
       {std::string("{}"), std::string("{\"items\":[,]}"), page(R"({"id":0,"title":"a"})"),
        page(R"({"id":1,"title":"\uD800"})"), page(R"({"id":1,"id":2,"title":"a"})"), page(R"({"id":1,"title":"a",})"),
        page(R"({"id":1,"title":false})"), page(R"({"id":1,"title":"a","x":01})"),
        page("{\"id\":1,\"title\":\"" + std::string(2049, 'x') + "\"}"),
        page(R"({"id":1,"title":"a"})") + " garbage"}) {
    Reply reply;
    reply.body = body;
    server::Catalog catalog(reply);
    ASSERT_TRUE(catalog.start({}));
    static CatalogRow row;
    while (catalog.next(row) == CatalogStep::Record) {
    }
    EXPECT_NE(catalog.error(), CatalogError::None) << body.substr(0, 100);
    EXPECT_EQ(reply.closes, 1u);
  }
}
TEST(Catalog, EveryTruncatedPrefixFails) {
  const auto body = page(R"({"id":1,"title":"a","files":[]})");
  for (size_t n = 0; n < body.size(); ++n) {
    Reply reply;
    reply.body = body.substr(0, n);
    server::Catalog catalog(reply);
    ASSERT_TRUE(catalog.start({}));
    static CatalogRow row;
    while (catalog.next(row) == CatalogStep::Record) {
    }
    EXPECT_NE(catalog.error(), CatalogError::None) << n;
  }
}
TEST(Catalog, PermissionLossAtOpenAndAtEnd) {
  for (int code : {401, 403}) {
    Reply reply;
    reply.code = code;
    server::Catalog catalog(reply);
    EXPECT_FALSE(catalog.start({}));
    EXPECT_EQ(catalog.error(), CatalogError::PermissionLost);
    reply.code = 200;
    reply.body = page(R"({"id":1,"title":"a"})");
    reply.terminal = code;
    ASSERT_TRUE(catalog.start({}));
    static CatalogRow row;
    ASSERT_EQ(catalog.next(row), CatalogStep::Record);
    EXPECT_EQ(catalog.next(row), CatalogStep::Error);
    EXPECT_EQ(catalog.error(), CatalogError::PermissionLost);
  }
}
TEST(Catalog, RestartIsExplicitAndNeverEmptySuccess) {
  Reply reply;
  reply.body = R"({"items":[],"hasNext":false,"nextCursor":null,"restartRequired":true})";
  server::Catalog catalog(reply);
  ASSERT_TRUE(catalog.start({}));
  static CatalogRow row;
  EXPECT_EQ(catalog.next(row), CatalogStep::Error);
  EXPECT_EQ(catalog.error(), CatalogError::RestartRequired);
}
#include "test/fixtures/catalog/FakeServer.h"
TEST(Catalog, BoundedTenThousandIngestAndRestart) {
  card = {};
  CatalogFakeServer reply;
  server::Catalog catalog(reply);
  storage::Index index({"/index", "/index.tmp", "/index.bak"}, "/a", "/b");
  ReadModel model(index);
  DeviceIndex worker(index, model);
  CatalogIndexSource binding(catalog, catalogFixtureBinding, nullptr);
  ASSERT_TRUE(catalog.start({}));
  ASSERT_TRUE(worker.start(binding.source(), {1, 1, 7, 0}));
  unsigned ticks = 0;
  while (worker.busy()) {
    ASSERT_TRUE(worker.tick());
    ASSERT_LE(worker.lastWork(), 16);
    ++ticks;
  }
  EXPECT_EQ(ticks, 626u);
  EXPECT_EQ(index.count(), 10000u);
  EXPECT_EQ(reply.requests, 50u);
  EXPECT_LE(reply.maxRead, 7u);
  EXPECT_EQ(reply.requests, reply.closes);
  card.restart();
  storage::Index recovered({"/index", "/index.tmp", "/index.bak"}, "/a", "/b");
  ASSERT_TRUE(recovered.recover());
  EXPECT_EQ(recovered.count(), 10000u);
  static storage::IndexRecord record;
  ASSERT_TRUE(recovered.read({recovered.provenance(), 9999}, record));
  EXPECT_STREQ(record.title.data(), "Synthetic book 10000");
  EXPECT_STREQ(record.path.data(), "/books/10000.epub");
  EXPECT_FALSE(storage::valid(record.source));
  // No whole response/tree or dynamic container: object sizes are independent of N.
  EXPECT_LT(sizeof(catalog), 5000u);
  EXPECT_LT(sizeof(binding), 11000u);
}
TEST(Catalog, FailedLaterPageRetainsPublishedGeneration) {
  for (bool atOpen : {false, true}) {
    card = {};
    CatalogFakeServer reply;
    reply.total = 1;
    server::Catalog catalog(reply);
    storage::Index index({"/index", "/index.tmp", "/index.bak"}, "/a", "/b");
    ReadModel model(index);
    DeviceIndex worker(index, model);
    CatalogIndexSource binding(catalog, catalogFixtureBinding, nullptr);
    ASSERT_TRUE(catalog.start({}));
    ASSERT_TRUE(worker.start(binding.source(), {1, 1, 7, 0}));
    while (worker.busy()) ASSERT_TRUE(worker.tick());
    reply.requests = 0;
    reply.total = 10000;
    if (atOpen)
      reply.denyPage = 2;
    else
      reply.breakPage = 2;
    ASSERT_TRUE(catalog.start({}));
    ASSERT_TRUE(worker.start(binding.source(), {2, 1, 7, 0}));
    bool ok = true;
    while (worker.busy() && ok) ok = worker.tick();
    EXPECT_FALSE(ok);
    EXPECT_EQ(catalog.error(), CatalogError::PermissionLost);
    card.restart();
    storage::Index recovered({"/index", "/index.tmp", "/index.bak"}, "/a", "/b");
    ASSERT_TRUE(recovered.recover());
    EXPECT_EQ(recovered.count(), 1u);
    EXPECT_EQ(recovered.provenance().generation, 1u);
  }
}
TEST(Catalog, RejectsNonadvancingRowsAndBounds) {
  for (const auto& body : {std::string(R"({"items":[{"id":1,"title":"a"},{"id":1,"title":"b"}],"hasNext":false})"),
                           std::string(R"({"items":[],"hasNext":true,"nextCursor":"x"})"),
                           page(R"({"id":1,"title":"a","authors":["a","b","c","d","e","f","g","h","i"]})"),
                           page(R"({"id":1,"title":"a","x":[[[[[[[[[[[[[0]]]]]]]]]]]]]})")}) {
    Reply reply;
    reply.body = body;
    server::Catalog catalog(reply);
    ASSERT_TRUE(catalog.start({}));
    static CatalogRow row;
    while (catalog.next(row) == CatalogStep::Record) {
    }
    EXPECT_NE(catalog.error(), CatalogError::None);
  }
}
TEST(Catalog, QueryEscapesAndCancelClosesExactlyOnce) {
  Reply reply;
  server::Catalog catalog(reply);
  ASSERT_TRUE(catalog.start({"device &/+", "original"}));
  EXPECT_EQ(reply.request, "/koreader/plugin/catalog/manifest?size=200&deviceId=device%20%26%2F%2B&variant=original");
  catalog.cancel();
  catalog.cancel();
  EXPECT_EQ(reply.closes, 1u);
  EXPECT_EQ(catalog.error(), CatalogError::Canceled);
}
TEST(Catalog, RejectsReadFailureAndInvalidUtf8) {
  Reply reply;
  reply.body = page(R"({"id":1,"title":"a"})");
  reply.cut = 17;
  server::Catalog catalog(reply);
  ASSERT_TRUE(catalog.start({}));
  static CatalogRow row;
  EXPECT_EQ(catalog.next(row), CatalogStep::Error);
  EXPECT_EQ(catalog.error(), CatalogError::Unavailable);
  reply.cut = SIZE_MAX;
  reply.body = page("{\"id\":1,\"title\":\"\xc0\x80\"}");
  ASSERT_TRUE(catalog.start({}));
  EXPECT_EQ(catalog.next(row), CatalogStep::Error);
}
TEST(Catalog, NormalizesMetadataAndCompleteness) {
  Reply reply;
  reply.body =
      R"({"items":[{"id":7,"title":"A","readStatus":"on_hold","progressPercentage":25.5,"hasCover":false,"seriesId":9,"lastReadAt":null}],"total":1})";
  server::Catalog catalog(reply);
  ASSERT_TRUE(catalog.start({{}, {}, server::CatalogFeed::Metadata}));
  static CatalogRow row;
  ASSERT_EQ(catalog.next(row), CatalogStep::Record);
  EXPECT_EQ(row.reading, CatalogReading::InProgress);
  EXPECT_DOUBLE_EQ(row.fraction, .255);
  EXPECT_TRUE(row.artworkKnown);
  EXPECT_FALSE(row.artworkPresent);
  EXPECT_STREQ(row.seriesAlias.data(), "9");
  EXPECT_EQ(catalog.next(row), CatalogStep::End);
  EXPECT_EQ(reply.request, "/koreader/plugin/catalog/books?size=100&page=1");
  reply.body = R"({"items":[],"total":1})";
  ASSERT_TRUE(catalog.start({{}, {}, server::CatalogFeed::Metadata}));
  EXPECT_EQ(catalog.next(row), CatalogStep::Error);
}
TEST(Catalog, RejectsRowAndPageCapacityWithoutPublishingEnd) {
  Reply reply;
  std::string fields;
  for (unsigned i = 0; i < 9; ++i) fields += ",\"unknown" + std::to_string(i) + "\":\"" + std::string(2000, 'x') + "\"";
  reply.body = page("{\"id\":1,\"title\":\"a\"" + fields + "}");
  server::Catalog catalog(reply);
  ASSERT_TRUE(catalog.start({}));
  static CatalogRow row;
  EXPECT_EQ(catalog.next(row), CatalogStep::Error);
  EXPECT_EQ(catalog.error(), CatalogError::Oversized);
  reply.body = "{\"items\":[";
  for (unsigned i = 1; i <= 201; ++i)
    reply.body += (i == 1 ? "" : ",") + std::string("{\"id\":") + std::to_string(i) + ",\"title\":\"a\"}";
  reply.body += "],\"hasNext\":false}";
  ASSERT_TRUE(catalog.start({}));
  unsigned count = 0;
  while (catalog.next(row) == CatalogStep::Record) ++count;
  EXPECT_EQ(count, 200u);
  EXPECT_EQ(catalog.error(), CatalogError::Oversized);
}
TEST(Catalog, RejectsMoreThanTenThousandBooks) {
  CatalogFakeServer reply;
  reply.total = 10001;
  server::Catalog catalog(reply);
  ASSERT_TRUE(catalog.start({}));
  static CatalogRow row;
  unsigned count = 0;
  while (catalog.next(row) == CatalogStep::Record) ++count;
  EXPECT_EQ(count, 10000u);
  EXPECT_EQ(catalog.error(), CatalogError::Oversized);
}
TEST(Catalog, DisplayTruncationPreservesUtf8) {
  Reply reply;
  reply.body = page("{\"id\":1,\"title\":\"" + std::string(94, 'a') + "🚀end\"}");
  server::Catalog catalog(reply);
  ASSERT_TRUE(catalog.start({}));
  static CatalogRow row;
  ASSERT_EQ(catalog.next(row), CatalogStep::Record);
  EXPECT_TRUE(row.titleTruncated);
  EXPECT_EQ(std::strlen(row.title.data()), 94u);
  EXPECT_EQ(catalog.next(row), CatalogStep::End);
}
TEST(Catalog, PreservesRenditionAndNullableTransferEvidence) {
  Reply reply;
  reply.body = page(
      R"({"id":1,"title":"a","files":[{"id":2,"format":"epub","downloadVariant":"audioless_epub","requestedVariant":"audioless_epub","sizeBytes":null,"fileHash":null,"selection":"opaque-selection","sourceMediaOverlayAvailable":true,"narrationPlaybackAvailable":false}]})");
  server::Catalog catalog(reply);
  ASSERT_TRUE(catalog.start({}));
  static CatalogRow row;
  ASSERT_EQ(catalog.next(row), CatalogStep::Record);
  EXPECT_STREQ(row.files[0].rendition.data(), "audioless_epub");
  EXPECT_FALSE(row.files[0].sizeKnown);
  EXPECT_STREQ(row.files[0].selection.data(), "opaque-selection");
  EXPECT_TRUE(row.files[0].sourceAudio);
  EXPECT_FALSE(row.files[0].playableAudio);
  EXPECT_EQ(catalog.next(row), CatalogStep::End);
}
TEST(Catalog, UnresolvedBindingCancelsAndPreservesIndex) {
  card = {};
  Reply reply;
  reply.body = page(R"({"id":1,"title":"a"})");
  server::Catalog catalog(reply);
  storage::Index index({"/index", "/index.tmp", "/index.bak"}, "/a", "/b");
  ReadModel model(index);
  DeviceIndex worker(index, model);
  CatalogIndexSource binding(catalog, [](void*, const CatalogRow&, storage::IndexRecord&) { return false; }, nullptr);
  ASSERT_TRUE(catalog.start({}));
  ASSERT_TRUE(worker.start(binding.source(), {1, 1, 7, 0}));
  EXPECT_FALSE(worker.tick());
  EXPECT_FALSE(index.ready());
  EXPECT_EQ(catalog.error(), CatalogError::Canceled);
  EXPECT_EQ(reply.closes, 1u);
}
TEST(Catalog, BoundsTotalResponseEvenForWhitespace) {
  struct Whitespace final : server::CatalogTransport {
    size_t n = 0;
    int open(std::string_view) override { return 200; }
    int read(char* out, size_t capacity) override {
      memset(out, ' ', capacity);
      n += capacity;
      return capacity;
    }
    int finish() override { return 200; }
    void close() override {}
  } reply;
  server::Catalog catalog(reply);
  ASSERT_TRUE(catalog.start({}));
  static CatalogRow row;
  EXPECT_EQ(catalog.next(row), CatalogStep::Error);
  EXPECT_EQ(catalog.error(), CatalogError::Oversized);
  EXPECT_LE(reply.n, server::Catalog::MaxPageBytes + 128);
}
TEST(Catalog, MetadataPagesPreserveServerOrderAndRejectChangedTotals) {
  struct Paged final : server::CatalogTransport {
    std::string body, request;
    size_t offset = 0;
    unsigned page = 0;
    bool changed = false;
    int open(std::string_view path) override {
      request = path;
      offset = 0;
      ++page;
      body = "{\"items\":[";
      const unsigned count = page == 1 ? 100 : 1;
      for (unsigned i = 0; i < count; ++i)
        body +=
            (i ? "," : "") + std::string("{\"id\":") + std::to_string(page == 1 ? 200 - i : 1) + ",\"title\":\"a\"}";
      body += "],\"total\":" + std::to_string(changed && page == 2 ? 102 : 101) + "}";
      return 200;
    }
    int read(char* out, size_t capacity) override {
      size_t n = std::min(capacity, body.size() - offset);
      memcpy(out, body.data() + offset, n);
      offset += n;
      return n;
    }
    int finish() override { return 200; }
    void close() override {}
  } reply;
  server::Catalog catalog(reply);
  ASSERT_TRUE(catalog.start({{}, {}, server::CatalogFeed::Metadata}));
  static CatalogRow row;
  unsigned count = 0;
  while (catalog.next(row) == CatalogStep::Record) ++count;
  EXPECT_EQ(count, 101u);
  EXPECT_EQ(catalog.error(), CatalogError::None);
  EXPECT_STREQ(row.alias.data(), "1");
  EXPECT_EQ(reply.request, "/koreader/plugin/catalog/books?size=100&page=2");
  reply.page = 0;
  reply.changed = true;
  ASSERT_TRUE(catalog.start({{}, {}, server::CatalogFeed::Metadata}));
  while (catalog.next(row) == CatalogStep::Record) {
  }
  EXPECT_EQ(catalog.error(), CatalogError::Invalid);
}
// Count C++ allocations only around production cursor calls; fake transport uses
// fixed segments. Index HAL test storage has its own host-only dynamic containers.
#include <atomic>
#include <cstdlib>
#include <new>
namespace {
std::atomic<bool> countCatalogAllocations{false};
std::atomic<size_t> catalogAllocationBytes{0};
}  // namespace
void* operator new(std::size_t size) {
  if (countCatalogAllocations) catalogAllocationBytes += size;
  if (void* p = std::malloc(size ? size : 1)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
TEST(Catalog, TenThousandCursorHasNoCppHeapAllocations) {
  CatalogFakeServer reply;
  server::Catalog catalog(reply);
  static CatalogRow row;
  catalogAllocationBytes = 0;
  countCatalogAllocations = true;
  const bool started = catalog.start({});
  unsigned count = 0;
  CatalogStep step = CatalogStep::Error;
  if (started)
    while ((step = catalog.next(row)) == CatalogStep::Record) ++count;
  countCatalogAllocations = false;
  EXPECT_TRUE(started);
  EXPECT_EQ(step, CatalogStep::End);
  EXPECT_EQ(count, 10000u);
  EXPECT_EQ(catalogAllocationBytes, 0u);
}
TEST(Catalog, PreservesFullLengthDigestHintWithoutAssertingRevision) {
  Reply reply;
  const std::string digest(64, 'a');
  reply.body =
      page("{\"id\":1,\"title\":\"a\",\"files\":[{\"id\":2,\"format\":\"epub\",\"fileHash\":\"" + digest + "\"}]}");
  server::Catalog catalog(reply);
  ASSERT_TRUE(catalog.start({}));
  static CatalogRow row;
  ASSERT_EQ(catalog.next(row), CatalogStep::Record);
  EXPECT_EQ(std::string(row.files[0].digest.data()), digest);
}
TEST(Catalog, PercentageNumbersAreBoundedAndHandleExponents) {
  for (const auto& token : {"25.5", "2.55e1", "255e-1", "100", "0e99999999999999999999", "1e-99999999999999999999"}) {
    Reply reply;
    reply.body =
        "{\"items\":[{\"id\":1,\"title\":\"a\",\"progressPercentage\":" + std::string(token) + "}],\"total\":1}";
    server::Catalog catalog(reply);
    ASSERT_TRUE(catalog.start({{}, {}, server::CatalogFeed::Metadata}));
    static CatalogRow row;
    ASSERT_EQ(catalog.next(row), CatalogStep::Record) << token;
    EXPECT_TRUE(row.fractionKnown);
    EXPECT_GE(row.fraction, 0);
    EXPECT_LE(row.fraction, 1);
    if (token[0] == '2') EXPECT_DOUBLE_EQ(row.fraction, .255);
    EXPECT_EQ(catalog.next(row), CatalogStep::End);
  }
  for (const auto& token : {"-1", "101", "1e99999999999999999999", "1e3"}) {
    Reply reply;
    reply.body =
        "{\"items\":[{\"id\":1,\"title\":\"a\",\"progressPercentage\":" + std::string(token) + "}],\"total\":1}";
    server::Catalog catalog(reply);
    ASSERT_TRUE(catalog.start({{}, {}, server::CatalogFeed::Metadata}));
    static CatalogRow row;
    EXPECT_EQ(catalog.next(row), CatalogStep::Error) << token;
  }
}
