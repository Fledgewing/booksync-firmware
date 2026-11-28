// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include <cstring>

#include "../../fixtures/CoverJpeg.h"
#include "booksync/server/bookorbit/Cover.h"
#include "booksync/services/CoverJobs.h"
using namespace booksync;
namespace {
constexpr DeliveryTicket owner{7, 1};
struct Covers : testing::Test {
  Admission admission{{nullptr, [](void*) { return MemorySnapshot{128 * 1024, 64 * 1024, 0}; },
                       [](void*) { return true; }, [](void*) { return true; }, [](void*) { return true; }},
                      true};
  struct Source : CoverSource {
    size_t pos = 0;
    unsigned opens = 0, reads = 0, closes = 0;
    uint64_t length = sizeof(CoverJpeg);
    bool known = true, finishOk = true, wrong = false;
    int badRead = 0;
    bool open(const CoverSpec& s, CoverReply& r) override {
      ++opens;
      pos = 0;
      r.selection = s;
      if (wrong) ++r.selection.revision[0];
      r.bytes = length;
      r.sizeKnown = known;
      return true;
    }
    int read(void* out, size_t capacity) override {
      ++reads;
      if (badRead) return badRead;
      const auto n = std::min(capacity, sizeof(CoverJpeg) - pos);
      std::memcpy(out, CoverJpeg + pos, n);
      pos += n;
      return int(n);
    }
    bool finish() override { return finishOk; }
    void close() override { ++closes; }
  } source;
  CoverSpec spec{};
  void SetUp() override {
    card = {};
    admission.setReading(false);
    std::strcpy(spec.server.data(), "server-token");
    std::strcpy(spec.account.data(), "account-token");
    std::strcpy(spec.book.data(), "opaque:book");
    spec.revision[0] = 1;
  }
  void complete(CoverJobs& j, DeliveryTicket ticket = owner) {
    for (unsigned i = 0; i < 600 && j.busy(); ++i) j.tick(ticket);
  }
  uint64_t start(CoverJobs& j) { return j.request(spec, source, owner); }
};
TEST_F(Covers, QueuedRequestDoesNoIoUntilPostPaintTick) {
  CoverJobs j(admission, "/cover.tmp");
  auto id = start(j);
  ASSERT_NE(id, 0u);
  EXPECT_EQ(source.opens, 0u);
  EXPECT_TRUE(card.events.empty());
  complete(j);
  EXPECT_EQ(j.result(id, owner), DeliveryResult::Ready);
  const auto ref = j.reference(id, owner);
  CoverBitmap bitmap;
  ASSERT_TRUE(j.copy(ref, owner, bitmap));
  EXPECT_EQ(bitmap.width, 16);
  EXPECT_EQ(bitmap.height, 16);
  EXPECT_EQ(bitmap.pixels[0], 0);
  EXPECT_EQ(card.handles, 0);
  auto events = card.events.size();
  EXPECT_EQ(start(j), id);
  EXPECT_TRUE(j.copy(ref, owner, bitmap));
  EXPECT_EQ(source.opens, 1u);
  EXPECT_EQ(card.events.size(), events);
}
TEST_F(Covers, RevisionInvalidatesOldReferenceBeforeAcquisition) {
  CoverJobs j(admission, "/cover.tmp");
  auto first = start(j);
  complete(j);
  auto old = j.reference(first, owner);
  ++spec.revision[0];
  auto second = start(j);
  CoverBitmap out;
  EXPECT_NE(first, second);
  EXPECT_FALSE(j.copy(old, owner, out));
  complete(j);
  EXPECT_EQ(j.result(second, owner), DeliveryResult::Ready);
  EXPECT_EQ(source.opens, 2u);
}
TEST_F(Covers, ScopeAndEpochNeverReuseCachedReference) {
  CoverJobs j(admission, "/cover.tmp");
  auto id = start(j);
  complete(j);
  auto old = j.reference(id, owner);
  CoverBitmap out;
  std::strcpy(spec.account.data(), "other-account");
  EXPECT_EQ(start(j), 0u);
  j.adopt({7, 2});
  EXPECT_FALSE(j.copy(old, {7, 2}, out));
  EXPECT_EQ(j.result(id, {7, 2}), DeliveryResult::Canceled);
  auto next = j.request(spec, source, {7, 2});
  EXPECT_NE(next, 0u);
  complete(j, {7, 2});
  EXPECT_FALSE(j.copy(old, owner, out));
  EXPECT_EQ(source.opens, 2u);
}
TEST_F(Covers, RetiredViewLeavesCacheWorkButNeverRedraws) {
  CoverJobs j(admission, "/cover.tmp");
  auto id = start(j);
  unsigned draws = 0;
  Delivery ticket(owner, 0, DeliveryPolicy::Owner,
                  {[](void* p, DeliveryResult) { ++*static_cast<unsigned*>(p); }, &draws});
  ticket.retire();
  complete(j);
  ticket.poll(1, j.result(id, owner), owner);
  EXPECT_EQ(draws, 0u);
  EXPECT_EQ(j.result(id, owner), DeliveryResult::Ready);
}
TEST_F(Covers, DeclaredOversizeAndWrongRevisionNeverReadBody) {
  CoverJobs j(admission, "/cover.tmp");
  source.length = CoverJobs::MaxInput + 1;
  auto id = start(j);
  complete(j);
  EXPECT_EQ(source.reads, 0u);
  EXPECT_EQ(j.result(id, owner), DeliveryResult::Unavailable);
  source.length = sizeof(CoverJpeg);
  source.wrong = true;
  id = start(j);
  complete(j);
  EXPECT_EQ(source.reads, 0u);
  EXPECT_EQ(j.result(id, owner), DeliveryResult::Unavailable);
}
TEST_F(Covers, TruncatedBodyAndFailedFramingNeverPublish) {
  CoverJobs j(admission, "/cover.tmp");
  ++source.length;
  auto id = start(j);
  complete(j);
  EXPECT_EQ(j.result(id, owner), DeliveryResult::Unavailable);
  --source.length;
  source.finishOk = false;
  id = start(j);
  complete(j);
  EXPECT_EQ(j.result(id, owner), DeliveryResult::Unavailable);
  EXPECT_EQ(card.handles, 0);
}
TEST_F(Covers, UntrustedReadLengthCannotOverflowScratch) {
  CoverJobs j(admission, "/cover.tmp");
  source.known = false;
  source.badRead = CoverJobs::Chunk + 1;
  auto id = start(j);
  complete(j);
  EXPECT_EQ(j.result(id, owner), DeliveryResult::Unavailable);
  EXPECT_EQ(card.handles, 0);
}
TEST_F(Covers, ReadingAndAccountTransitionAbortBeforePublication) {
  CoverJobs j(admission, "/cover.tmp");
  auto id = start(j);
  j.tick(owner);
  admission.setReading(true);
  complete(j);
  EXPECT_NE(j.result(id, owner), DeliveryResult::Ready);
  admission.setReading(false);
  id = start(j);
  j.tick(owner);
  j.adopt({9, 2});
  complete(j, {9, 2});
  EXPECT_EQ(j.result(id, owner), DeliveryResult::Canceled);
  EXPECT_EQ(card.handles, 0);
}
TEST_F(Covers, HtmlAndHugeDimensionsFailBoundedDecoder) {
  card.files["/image"] = "<html>failure</html>";
  CoverDecode decoder;
  CoverBitmap out;
  EXPECT_FALSE(decoder.decode("/image", out));
  card.files["/image"] = std::string(reinterpret_cast<const char*>(CoverJpeg), sizeof(CoverJpeg));
  auto& bytes = card.files["/image"];
  for (size_t i = 0; i + 9 < bytes.size(); ++i)
    if (uint8_t(bytes[i]) == 0xff && uint8_t(bytes[i + 1]) == 0xc0) {
      bytes[i + 5] = 0x7f;
      bytes[i + 6] = char(0xff);
      break;
    }
  EXPECT_FALSE(decoder.decode("/image", out));
  EXPECT_EQ(card.handles, 0);
}
}  // namespace
namespace {
TEST_F(Covers, UnknownLengthStreamStopsAtInputLimit) {
  struct Endless : CoverSource {
    size_t bytes = 0;
    bool open(const CoverSpec& s, CoverReply& r) override {
      r.selection = s;
      return true;
    }
    int read(void* p, size_t n) override {
      std::memset(p, 0, n);
      bytes += n;
      return static_cast<int>(n);
    }
    bool finish() override { return true; }
    void close() override {}
  } endless;
  CoverJobs j(admission, "/cover.tmp");
  auto id = j.request(spec, endless, owner);
  complete(j);
  EXPECT_EQ(j.result(id, owner), DeliveryResult::Unavailable);
  EXPECT_LE(endless.bytes, CoverJobs::MaxInput + CoverJobs::Chunk);
  EXPECT_EQ(card.handles, 0);
}
TEST_F(Covers, FullCardNeverPublishesAndReleasesLease) {
  CoverJobs j(admission, "/cover.tmp");
  auto id = start(j);
  j.tick(owner);
  card.full = true;
  complete(j);
  EXPECT_EQ(j.result(id, owner), DeliveryResult::Unavailable);
  EXPECT_FALSE(admission.busy());
  EXPECT_EQ(card.handles, 0);
}
TEST_F(Covers, CacheEvictionAndArtworkRemovalInvalidateOpaqueIds) {
  CoverJobs j(admission, "/cover.tmp");
  auto id = start(j);
  complete(j);
  auto ref = j.reference(id, owner);
  for (size_t i = 0; i < CoverJobs::Slots; ++i) {
    spec.book[0] = char('a' + i);
    start(j);
    complete(j);
  }
  CoverBitmap out;
  EXPECT_FALSE(j.copy(ref, owner, out));
  id = start(j);
  ref = j.reference(id, owner);
  spec.revision.fill(0);
  j.observe(spec, owner);
  EXPECT_FALSE(j.copy(ref, owner, out));
  EXPECT_EQ(start(j), 0u);
}
TEST_F(Covers, BusyRequestCannotEvictInFlightCover) {
  CoverJobs j(admission, "/cover.tmp");
  auto id = start(j);
  spec.book[0] = 'z';
  EXPECT_EQ(start(j), 0u);
  complete(j);
  EXPECT_EQ(j.result(id, owner), DeliveryResult::Ready);
}
struct Transport final : server::CoverTransport {
  CoverSpec selection{};
  const char* path = "/fixture/cover";
  int status = 200;
  bool jpeg = true, resolved = true, authorized = true;
  unsigned opens = 0, closes = 0;
  bool resolve(const CoverSpec&, server::CoverTarget& target) override {
    target.selection = selection;
    std::strcpy(target.path.data(), path);
    return resolved;
  }
  bool open(const char*, server::CoverHeaders& h) override {
    ++opens;
    h.status = status;
    h.jpeg = jpeg;
    return true;
  }
  int read(void*, size_t) override { return 0; }
  bool finish() override { return authorized; }
  void close() override { ++closes; }
};
TEST_F(Covers, AdapterRejectsForeignTargetsRedirectsAndContentTypes) {
  Transport transport;
  transport.selection = spec;
  server::Cover adapter(transport);
  CoverReply reply;
  for (auto path : {"scheme:/cover", "//invalid/cover", "/fixture\\cover", "/fixture#fragment", "/fixture\r\nheader"}) {
    transport.path = path;
    EXPECT_FALSE(adapter.open(spec, reply));
    EXPECT_EQ(transport.opens, 0u);
  }
  transport.path = "/fixture/cover";
  transport.selection.revision[0]++;
  EXPECT_FALSE(adapter.open(spec, reply));
  EXPECT_EQ(transport.opens, 0u);
  transport.selection = spec;
  transport.status = 302;
  EXPECT_FALSE(adapter.open(spec, reply));
  transport.status = 200;
  transport.jpeg = false;
  EXPECT_FALSE(adapter.open(spec, reply));
  transport.jpeg = true;
  ASSERT_TRUE(adapter.open(spec, reply));
  transport.authorized = false;
  EXPECT_FALSE(adapter.finish());
  adapter.close();
  EXPECT_GE(transport.closes, 5u);
}
TEST_F(Covers, DecoderRejectsProgressiveAndCanceledDecode) {
  auto bytes = std::string(reinterpret_cast<const char*>(CoverJpeg), sizeof(CoverJpeg));
  for (size_t i = 0; i + 1 < bytes.size(); ++i)
    if (uint8_t(bytes[i]) == 0xff && uint8_t(bytes[i + 1]) == 0xc0) {
      bytes[i + 1] = char(0xc2);
      break;
    }
  card.files["/image"] = bytes;
  CoverDecode decoder;
  CoverBitmap out;
  EXPECT_FALSE(decoder.decode("/image", out));
  card.files["/image"] = std::string(reinterpret_cast<const char*>(CoverJpeg), sizeof(CoverJpeg));
  EXPECT_FALSE(decoder.decode("/image", out, [](void*) { return false; }));
  EXPECT_EQ(card.handles, 0);
}
}  // namespace
namespace {
TEST_F(Covers, OddSizedJpegScalesWithinPackedOutputBounds) {
  card.files["/odd"] = std::string(reinterpret_cast<const char*>(CoverOddJpeg), sizeof(CoverOddJpeg));
  CoverDecode decoder;
  struct Guarded {
    CoverBitmap bitmap;
    uint64_t canary = 0x5aa55aa5;
  } out;
  ASSERT_TRUE(decoder.decode("/odd", out.bitmap));
  EXPECT_LE(out.bitmap.width, CoverWidth);
  EXPECT_LE(out.bitmap.height, CoverHeight);
  EXPECT_EQ(out.bitmap.width, 85);
  EXPECT_EQ(out.bitmap.height, 129);
  const auto stride = (out.bitmap.width + 7) / 8;
  EXPECT_EQ(out.bitmap.pixels[0], 0);
  EXPECT_EQ(out.bitmap.pixels[(out.bitmap.height - 1) * stride], 0);
  EXPECT_EQ(out.canary, 0x5aa55aa5u);
  EXPECT_EQ(card.handles, 0);
}
}  // namespace
namespace {
TEST_F(Covers, InFlightRevisionInvalidationDoesNoIoUntilTick) {
  CoverJobs j(admission, "/cover.tmp");
  auto id = start(j);
  j.tick(owner);
  const auto events = card.events.size();
  const auto closes = source.closes;
  ++spec.revision[0];
  EXPECT_EQ(start(j), 0u);
  EXPECT_EQ(card.events.size(), events);
  EXPECT_EQ(source.closes, closes);
  EXPECT_EQ(j.result(id, owner), DeliveryResult::Canceled);
  complete(j);
  EXPECT_EQ(card.handles, 0);
  EXPECT_NE(start(j), 0u);
  complete(j);
}
TEST_F(Covers, StaleEpochCannotReAdoptOldAccount) {
  CoverJobs j(admission, "/cover.tmp");
  auto id = start(j);
  complete(j);
  j.adopt({8, 2});
  j.adopt(owner);
  EXPECT_EQ(start(j), 0u);
  EXPECT_EQ(j.result(id, owner), DeliveryResult::Canceled);
  EXPECT_NE(j.request(spec, source, {8, 2}), 0u);
  complete(j, {8, 2});
}
}  // namespace
namespace {
TEST_F(Covers, FailedAdmissionKeepsDrainingUntilDisplayRestored) {
  bool stopped = false;
  Admission low{{&stopped, [](void*) { return MemorySnapshot{1024, 1024, 0}; }, [](void*) { return true; },
                 [](void*) { return true; }, [](void* p) { return *static_cast<bool*>(p); }},
                true};
  low.setReading(false);
  CoverJobs j(low, "/cover.tmp");
  auto id = start(j);
  j.tick(owner);
  EXPECT_TRUE(j.busy());
  EXPECT_FALSE(low.displayReady());
  EXPECT_EQ(source.opens, 0u);
  j.tick(owner);
  EXPECT_TRUE(j.busy());
  stopped = true;
  j.tick(owner);
  EXPECT_FALSE(j.busy());
  EXPECT_TRUE(low.displayReady());
  EXPECT_FALSE(low.busy());
  EXPECT_EQ(j.result(id, owner), DeliveryResult::Unavailable);
}
TEST_F(Covers, BusyAdmissionDoesNotFinishAnotherWorkersLease) {
  ASSERT_EQ(admission.begin(TAILNET_JOB_CATALOG), AdmissionResult::Admitted);
  auto resource = admission.acquire(Resource::Worker);
  CoverJobs j(admission, "/cover.tmp");
  auto id = start(j);
  j.tick(owner);
  EXPECT_FALSE(j.busy());
  EXPECT_TRUE(admission.permitted());
  EXPECT_EQ(source.opens, 0u);
  EXPECT_EQ(j.result(id, owner), DeliveryResult::Unavailable);
  resource.reset();
  EXPECT_TRUE(admission.finish());
}
}  // namespace
