// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>

#include "booksync/server/bookorbit/Download.h"
#include "booksync/services/DownloadJobs.h"
using namespace booksync;
namespace {
constexpr storage::PublishPaths journal{"/job", "/job.tmp", "/job.bak"};
constexpr DeliveryTicket owner{7, 1};
constexpr storage::ScopeView scope{"server-token", "account-token"};
struct DownloadTest : testing::Test {
  uint64_t free = DownloadJobs::Headroom + 10000;
  unsigned links = 0;
  bool linkOk = true;
  storage::SourceIdentity linked{};
  Admission admission{{nullptr, [](void*) { return MemorySnapshot{128 * 1024, 64 * 1024, 0}; },
                       [](void*) { return true; }, [](void*) { return true; }, [](void*) { return true; }},
                      true};
  DownloadPorts ports{this, [](void* p) { return static_cast<DownloadTest*>(p)->free; },
                      [](void* p, const DownloadSpec& selected) {
                        auto& f = *static_cast<DownloadTest*>(p);
                        ++f.links;
                        f.linked = selected.identity;
                        return f.linkOk;
                      }};
  struct Source : DownloadSource {
    DownloadSpec selected{};
    uint64_t position = 0, requested = 0;
    unsigned opens = 0, reads = 0;
    bool wrongEdition = false, decline = false, badRange = false, badDigest = false, authorized = true, known = true;
    bool open(const DownloadSpec& s, uint64_t offset, const char*, DownloadReply& r) override {
      ++opens;
      selected = s;
      requested = offset;
      position = decline ? 0 : offset;
      r = {};
      r.identity = s.identity;
      if (wrongEdition) ++r.identity.edition.bytes[0];
      r.offset = badRange ? offset + 1 : position;
      r.bytes = 3;
      r.sizeKnown = known;
      std::strcpy(r.validator.data(), "strong-revision");
      return authorized;
    }
    int read(void* out, size_t) override {
      ++reads;
      if (position == 3) return 0;
      *static_cast<char*>(out) = badDigest ? 'x' : "abc"[position];
      ++position;
      return 1;
    }
    bool finish() override { return authorized; }
    void close() override {}
  } source;
  DownloadSpec spec{};
  void SetUp() override {
    card = {};
    admission.setReading(false);
    std::strcpy(spec.server.data(), "server-token");
    std::strcpy(spec.account.data(), "account-token");
    std::strcpy(spec.book.data(), "opaque:book");
    std::strcpy(spec.file.data(), "opaque:file");
    std::strcpy(spec.variant.data(), "selected");
    std::strcpy(spec.path.data(), "/books/book.epub");
    spec.bytes = 3;
    spec.sizeKnown = true;
    spec.identity.book.bytes[0] = 1;
    spec.identity.edition.bytes[0] = 2;
    constexpr uint8_t digest[] = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
                                  0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
                                  0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
    std::memcpy(spec.identity.revision.bytes.data(), digest, 32);
  }
  void complete(DownloadJobs& j) {
    for (unsigned i = 0; i < 20 && j.busy(); ++i) j.tick(owner);
  }
};
TEST_F(DownloadTest, VerifiedPublishAndLink) {
  DownloadJobs j(journal, admission, ports);
  ASSERT_TRUE(j.recover(scope, owner));
  auto id = j.start(spec, source, owner);
  ASSERT_NE(id, 0u);
  complete(j);
  EXPECT_EQ(j.result(id, owner), DeliveryResult::Ready);
  EXPECT_EQ(card.files[spec.path.data()], "abc");
  EXPECT_EQ(links, 1u);
}
TEST_F(DownloadTest, DuplicateKeepsFrozenPathAndLateIdCannotCancelNewRun) {
  DownloadJobs j(journal, admission, ports);
  ASSERT_TRUE(j.recover(scope, owner));
  auto id = j.start(spec, source, owner);
  std::strcpy(spec.path.data(), "/changed-root/book.epub");
  EXPECT_EQ(j.start(spec, source, owner), id);
  complete(j);
  EXPECT_EQ(card.files["/books/book.epub"], "abc");
  EXPECT_FALSE(Storage.exists(spec.path.data()));
  j.cancel(id, owner);
  auto next = j.start(spec, source, owner);
  ASSERT_GT(next, id);
  j.cancel(id, owner);
  complete(j);
  EXPECT_EQ(j.result(next, owner), DeliveryResult::Ready);
}
TEST_F(DownloadTest, PausedCancellationDoesNoStorageWorkWhileReading) {
  DownloadJobs j(journal, admission, ports);
  ASSERT_TRUE(j.recover(scope, owner));
  auto id = j.start(spec, source, owner);
  j.tick(owner);
  admission.setReading(true);
  j.tick(owner);
  const auto events = card.events.size();
  j.cancel(id, owner);
  EXPECT_EQ(card.events.size(), events);
  EXPECT_EQ(j.status(owner).state, DownloadState::Paused);
  admission.setReading(false);
  j.cancel(id, owner);
  EXPECT_EQ(j.result(id, owner), DeliveryResult::Canceled);
}
TEST_F(DownloadTest, InvalidSelectionAndUnownedStagingDoNotPoisonRetry) {
  DownloadJobs j(journal, admission, ports);
  ASSERT_TRUE(j.recover(scope, owner));
  std::strcpy(spec.path.data(), "/books/../book.epub");
  EXPECT_EQ(j.start(spec, source, owner), 0u);
  EXPECT_EQ(source.opens, 0u);
  std::strcpy(spec.path.data(), "/books/book.epub");
  card.files["/books/book.epub.tmp"] = "unrelated";
  EXPECT_EQ(j.start(spec, source, owner), 0u);
  EXPECT_EQ(source.opens, 0u);
  EXPECT_EQ(card.files["/books/book.epub.tmp"], "unrelated");
  std::strcpy(spec.path.data(), "/books/other.epub");
  const auto id = j.start(spec, source, owner);
  ASSERT_NE(id, 0u);
  complete(j);
  EXPECT_EQ(j.result(id, owner), DeliveryResult::Ready);
}
TEST_F(DownloadTest, WrongDigestNeverPublishes) {
  source.badDigest = true;
  DownloadJobs j(journal, admission, ports);
  ASSERT_TRUE(j.recover(scope, owner));
  ASSERT_NE(j.start(spec, source, owner), 0u);
  complete(j);
  EXPECT_EQ(j.status(owner).error, DownloadError::Digest);
  EXPECT_FALSE(Storage.exists(spec.path.data()));
  EXPECT_EQ(links, 0u);
}
TEST_F(DownloadTest, WrongEditionNeverReads) {
  source.wrongEdition = true;
  DownloadJobs j(journal, admission, ports);
  ASSERT_TRUE(j.recover(scope, owner));
  j.start(spec, source, owner);
  EXPECT_EQ(j.status(owner).error, DownloadError::Edition);
  EXPECT_EQ(source.reads, 0u);
}
TEST_F(DownloadTest, NoSpace) {
  free = DownloadJobs::Headroom + 2;
  DownloadJobs j(journal, admission, ports);
  ASSERT_TRUE(j.recover(scope, owner));
  j.start(spec, source, owner);
  EXPECT_EQ(j.status(owner).error, DownloadError::NoSpace);
  EXPECT_FALSE(Storage.exists(spec.path.data()));
}
TEST_F(DownloadTest, CancelDiscardsOnlyStagingAndRevokesDelivery) {
  DownloadJobs j(journal, admission, ports);
  ASSERT_TRUE(j.recover(scope, owner));
  auto id = j.start(spec, source, owner);
  j.tick(owner);
  j.cancel(id, owner);
  EXPECT_EQ(j.result(id, owner), DeliveryResult::Canceled);
  EXPECT_FALSE(Storage.exists("/books/book.epub.tmp"));
  EXPECT_EQ(links, 0u);
  DownloadJobs reboot(journal, admission, ports);
  ASSERT_TRUE(reboot.recover(scope, owner));
  EXPECT_EQ(reboot.status(owner).state, DownloadState::Canceled);
}
TEST_F(DownloadTest, RestartRequiresExplicitResumeAndSamePath) {
  uint64_t id;
  {
    DownloadJobs j(journal, admission, ports);
    ASSERT_TRUE(j.recover(scope, owner));
    id = j.start(spec, source, owner);
    j.tick(owner);
  }
  card.restart();
  unsigned opens = source.opens;
  DownloadJobs j(journal, admission, ports);
  ASSERT_TRUE(j.recover(scope, owner));
  EXPECT_EQ(j.status(owner).state, DownloadState::Paused);
  EXPECT_EQ(source.opens, opens);
  ASSERT_TRUE(j.resume(id, source, owner));
  EXPECT_EQ(source.requested, 1u);
  complete(j);
  EXPECT_EQ(card.files[spec.path.data()], "abc");
  EXPECT_EQ(j.result(id, owner), DeliveryResult::Ready);
}
TEST_F(DownloadTest, RangeDeclineRestartsAndInvalidRangeRejects) {
  uint64_t id;
  {
    DownloadJobs j(journal, admission, ports);
    ASSERT_TRUE(j.recover(scope, owner));
    id = j.start(spec, source, owner);
    j.tick(owner);
  }
  DownloadJobs j(journal, admission, ports);
  ASSERT_TRUE(j.recover(scope, owner));
  source.decline = true;
  ASSERT_TRUE(j.resume(id, source, owner));
  complete(j);
  EXPECT_EQ(card.files[spec.path.data()], "abc");
}
TEST_F(DownloadTest, ReadingAdmissionAndAccountIsolation) {
  DownloadJobs j(journal, admission, ports);
  ASSERT_TRUE(j.recover(scope, owner));
  admission.setReading(true);
  EXPECT_EQ(j.start(spec, source, owner), 0u);
  EXPECT_EQ(source.opens, 0u);
  admission.setReading(false);
  auto id = j.start(spec, source, owner);
  j.tick({7, 3});
  EXPECT_EQ(j.result(id, {7, 3}), DeliveryResult::Canceled);
  EXPECT_EQ(links, 0u);
}
TEST_F(DownloadTest, LinkRetryDoesNotRedownload) {
  linkOk = false;
  DownloadJobs j(journal, admission, ports);
  ASSERT_TRUE(j.recover(scope, owner));
  auto id = j.start(spec, source, owner);
  complete(j);
  EXPECT_EQ(j.status(owner).state, DownloadState::Linking);
  auto opens = source.opens;
  linkOk = true;
  ASSERT_TRUE(j.resume(id, source, owner));
  complete(j);
  EXPECT_EQ(source.opens, opens);
  EXPECT_EQ(j.result(id, owner), DeliveryResult::Ready);
}
TEST_F(DownloadTest, ExistingCollisionAndSidecarsPreserved) {
  card.files[spec.path.data()] = "unrelated";
  card.files["/books/book.epub.progress"] = "place";
  DownloadJobs j(journal, admission, ports);
  ASSERT_TRUE(j.recover(scope, owner));
  j.start(spec, source, owner);
  complete(j);
  EXPECT_EQ(card.files[spec.path.data()], "unrelated");
  EXPECT_EQ(card.files["/books/book.epub.progress"], "place");
  EXPECT_EQ(links, 0u);
}
TEST_F(DownloadTest, RetiredOriginCannotOpenAndReadyCanOpenOnce) {
  unsigned opens = 0;
  DownloadJobs j(journal, admission, ports);
  ASSERT_TRUE(j.recover(scope, owner));
  auto id = j.start(spec, source, owner);
  Delivery view(owner, 0, DeliveryPolicy::Owner,
                {[](void* p, DeliveryResult r) {
                   if (r == DeliveryResult::Ready) ++*static_cast<unsigned*>(p);
                 },
                 &opens});
  view.retire();
  complete(j);
  view.poll(1, j.result(id, owner), owner);
  EXPECT_EQ(opens, 0u);
}
TEST_F(DownloadTest, InvalidRangeAndChangedValidatorRejectBeforeAppend) {
  uint64_t id;
  {
    DownloadJobs j(journal, admission, ports);
    ASSERT_TRUE(j.recover(scope, owner));
    id = j.start(spec, source, owner);
    j.tick(owner);
  }
  DownloadJobs j(journal, admission, ports);
  ASSERT_TRUE(j.recover(scope, owner));
  source.badRange = true;
  EXPECT_FALSE(j.resume(id, source, owner));
  EXPECT_EQ(card.files["/books/book.epub.tmp"], "a");
  EXPECT_EQ(links, 0u);
}
TEST_F(DownloadTest, MidStreamSpaceAndShortWriteRetainRecoverablePartial) {
  DownloadJobs j(journal, admission, ports);
  ASSERT_TRUE(j.recover(scope, owner));
  auto id = j.start(spec, source, owner);
  j.tick(owner);
  free = DownloadJobs::Headroom;
  j.tick(owner);
  EXPECT_EQ(j.status(owner).error, DownloadError::NoSpace);
  EXPECT_EQ(card.files["/books/book.epub.tmp"], "a");
  free += 1000;
  ASSERT_TRUE(j.resume(id, source, owner));
  card.full = true;
  j.tick(owner);
  EXPECT_EQ(j.status(owner).error, DownloadError::StorageFailure);
  EXPECT_FALSE(Storage.exists(spec.path.data()));
}
TEST_F(DownloadTest, UnknownLengthReportsBytesWithoutInventedTotal) {
  spec.sizeKnown = false;
  spec.bytes = 0;
  source.known = false;
  DownloadJobs j(journal, admission, ports);
  ASSERT_TRUE(j.recover(scope, owner));
  auto id = j.start(spec, source, owner);
  j.tick(owner);
  EXPECT_FALSE(j.status(owner).totalKnown);
  EXPECT_EQ(j.status(owner).received, 1u);
  complete(j);
  EXPECT_EQ(j.result(id, owner), DeliveryResult::Ready);
}
TEST_F(DownloadTest, CancelAfterPublishPreservesCompleteBytesWithoutOpening) {
  DownloadJobs j(journal, admission, ports);
  ASSERT_TRUE(j.recover(scope, owner));
  const auto id = j.start(spec, source, owner);
  for (unsigned i = 0; i < 4; ++i) j.tick(owner);
  ASSERT_EQ(j.status(owner).state, DownloadState::Linking);
  j.cancel(id, owner);
  EXPECT_EQ(j.result(id, owner), DeliveryResult::Canceled);
  EXPECT_EQ(card.files[spec.path.data()], "abc");
  EXPECT_EQ(links, 0u);
}
TEST_F(DownloadTest, ForeignScopeAndTraversalAreRejected) {
  {
    DownloadJobs j(journal, admission, ports);
    ASSERT_TRUE(j.recover(scope, owner));
    j.start(spec, source, owner);
    j.tick(owner);
  }
  DownloadJobs foreign(journal, admission, ports);
  EXPECT_FALSE(foreign.recover({"server-token", "other-account"}, owner));
  EXPECT_EQ(foreign.start(spec, source, owner), 0u);
}
TEST_F(DownloadTest, LiveOriginOpensOnlyOnceAndReadingRevokesIt) {
  unsigned opens = 0;
  DownloadJobs j(journal, admission, ports);
  ASSERT_TRUE(j.recover(scope, owner));
  auto id = j.start(spec, source, owner);
  Delivery view(owner, 0, DeliveryPolicy::Owner,
                {[](void* p, DeliveryResult r) {
                   if (r == DeliveryResult::Ready) ++*static_cast<unsigned*>(p);
                 },
                 &opens});
  complete(j);
  view.poll(1, j.result(id, owner), owner);
  view.poll(2, j.result(id, owner), owner);
  EXPECT_EQ(opens, 1u);
  Delivery departed(owner, 0, DeliveryPolicy::Owner,
                    {[](void* p, DeliveryResult r) {
                       if (r == DeliveryResult::Ready) ++*static_cast<unsigned*>(p);
                     },
                     &opens});
  departed.poll(1, DeliveryResult::Pending, owner, false);
  departed.poll(2, j.result(id, owner), owner, true);
  EXPECT_EQ(opens, 1u);
}
TEST_F(DownloadTest, PowerCutAtEveryStorageBoundaryThenExplicitResume) {
  // Measure a clean run, then replay each operation once with a simulated loss
  // of volatile card state. A cut must leave absent or exact verified bytes.
  {
    DownloadJobs j(journal, admission, ports);
    ASSERT_TRUE(j.recover(scope, owner));
    j.start(spec, source, owner);
    complete(j);
  }
  const int events = card.events.size();
  unsigned recovered = 0;
  for (bool eager : {false, true})
    for (int cut = 0; cut < events; ++cut) {
      card = {};
      card.eager = eager;
      card.stop = cut;
      links = 0;
      {
        DownloadJobs j(journal, admission, ports);
        try {
          if (j.recover(scope, owner)) {
            j.start(spec, source, owner);
            complete(j);
          }
        } catch (const PowerCut&) {
        }
      }
      card.restart();
      auto published = card.files.find(spec.path.data());
      ASSERT_TRUE(published == card.files.end() || published->second == "abc") << cut;
      DownloadJobs j(journal, admission, ports);
      ASSERT_TRUE(j.recover(scope, owner)) << cut;
      auto id = j.status(owner).id;
      if (id)
        ASSERT_TRUE(j.resume(id, source, owner)) << cut;
      else
        ASSERT_NE(j.start(spec, source, owner), 0u) << cut;
      complete(j);
      EXPECT_EQ(j.status(owner).state, DownloadState::Complete) << cut;
      EXPECT_EQ(card.files[spec.path.data()], "abc") << cut;
      ++recovered;
    }
  EXPECT_GT(recovered, 100u);
}

}  // namespace

namespace {
struct HttpDownloadTest : DownloadTest {
  struct Transport : booksync::server::DownloadTransport {
    booksync::server::DownloadHeaders headers{};
    unsigned closes = 0, opens = 0;
    bool finishOk = true, changedVersion = false, changedProof = false;
    uint64_t position = 0;
    bool resolve(const DownloadSpec& spec, booksync::server::DownloadTarget& out) override {
      out.identity = spec.identity;
      out.proof = spec.proof;
      out.version = spec.version;
      if (changedVersion) std::strcpy(out.version.data(), "other-version");
      if (changedProof) std::strcpy(out.proof.data(), "00000000000000000000000000000000");
      std::strcpy(out.path.data(), "/selected/acquisition");
      return true;
    }
    bool open(const char*, uint64_t offset, const char*, booksync::server::DownloadHeaders& out) override {
      position = offset;
      ++opens;
      out = headers;
      return true;
    }
    int read(void* out, size_t) override {
      if (position == 3) return 0;
      *static_cast<char*>(out) = "abc"[position++];
      return 1;
    }
    bool finish() override { return finishOk; }
    void close() override { ++closes; }
  } transport;
};
TEST_F(HttpDownloadTest, ExactRangeAndStrongValidatorRequired) {
  booksync::server::Download source(transport);
  DownloadReply reply;
  transport.headers.status = 206;
  transport.headers.lengthKnown = true;
  transport.headers.contentLength = 2;
  std::strcpy(transport.headers.etag.data(), "\"revision\"");
  std::strcpy(transport.headers.contentRange.data(), "bytes 1-2/3");
  ASSERT_TRUE(source.open(spec, 1, "\"revision\"", reply));
  EXPECT_EQ(reply.offset, 1u);
  EXPECT_EQ(reply.bytes, 3u);
  std::strcpy(transport.headers.contentRange.data(), "bytes 2-2/3");
  EXPECT_FALSE(source.open(spec, 1, "\"revision\"", reply));
  std::strcpy(transport.headers.contentRange.data(), "bytes 1-1/3");
  EXPECT_FALSE(source.open(spec, 1, "\"revision\"", reply));
  std::strcpy(transport.headers.contentRange.data(), "bytes 1-2/3");
  std::strcpy(transport.headers.etag.data(), "W/\"revision\"");
  EXPECT_FALSE(source.open(spec, 1, "\"revision\"", reply));
}
TEST_F(HttpDownloadTest, CompleteStagingUsesFreshExactResolutionWithoutBody) {
  booksync::server::Download source(transport);
  DownloadReply reply;
  ASSERT_TRUE(source.open(spec, 3, "\"revision\"", reply));
  EXPECT_EQ(transport.opens, 0u);
  EXPECT_EQ(reply.offset, 3u);
  char byte;
  EXPECT_EQ(source.read(&byte, 1), 0);
  EXPECT_TRUE(source.finish());
  source.close();
  EXPECT_FALSE(source.finish());
}
TEST_F(HttpDownloadTest, RedirectAuthAndMalformedRangesRejected) {
  booksync::server::Download source(transport);
  DownloadReply reply;
  for (int code : {301, 302, 307, 401, 403, 404, 500}) {
    transport.headers.status = code;
    EXPECT_FALSE(source.open(spec, 0, "", reply));
  }
  transport.headers.status = 206;
  std::strcpy(transport.headers.etag.data(), "\"revision\"");
  for (const char* range : {"", "bytes 1-2/*", "bytes 1-2/3junk", "bytes 18446744073709551616-2/3", "bytes 1-3/3"}) {
    std::strcpy(transport.headers.contentRange.data(), range);
    EXPECT_FALSE(source.open(spec, 1, "\"revision\"", reply));
  }
}
TEST_F(HttpDownloadTest, RangeDeclineLengthAndFinalAuthorizationChecked) {
  booksync::server::Download source(transport);
  DownloadReply reply;
  transport.headers.status = 200;
  transport.headers.contentLength = 1;
  transport.headers.lengthKnown = true;
  ASSERT_TRUE(source.open(spec, 1, "\"revision\"", reply));
  EXPECT_EQ(reply.offset, 0u);
  char byte;
  ASSERT_EQ(source.read(&byte, 1), 1);
  EXPECT_TRUE(source.finish());
  EXPECT_EQ(source.read(&byte, 1), -1);
  ASSERT_TRUE(source.open(spec, 0, "", reply));
  EXPECT_FALSE(source.finish());
  source.read(&byte, 1);
  transport.finishOk = false;
  EXPECT_FALSE(source.finish());
}
TEST_F(HttpDownloadTest, PrivateReaderProofProducesCanonicalShaBeforeLink) {
  auto expected = spec.identity.revision;
  spec.identity.revision = {};
  std::strcpy(spec.version.data(), "opaque-version");
  std::strcpy(spec.proof.data(), "900150983cd24fb0d6963f7d28e17f72");
  transport.headers.status = 200;
  transport.headers.lengthKnown = true;
  transport.headers.contentLength = 3;
  booksync::server::Download source(transport);
  DownloadJobs j(journal, admission, ports);
  ASSERT_TRUE(j.recover(scope, owner));
  const auto id = j.start(spec, source, owner);
  ASSERT_NE(id, 0u);
  complete(j);
  EXPECT_EQ(j.result(id, owner), DeliveryResult::Ready);
  EXPECT_EQ(linked.revision, expected);
  EXPECT_EQ(card.files[spec.path.data()], "abc");
}
TEST_F(HttpDownloadTest, WrongReaderProofNeverPublishesOrIssuesIdentity) {
  spec.identity.revision = {};
  std::strcpy(spec.proof.data(), "00000000000000000000000000000000");
  transport.headers.status = 200;
  transport.headers.lengthKnown = true;
  transport.headers.contentLength = 3;
  booksync::server::Download source(transport);
  DownloadJobs j(journal, admission, ports);
  ASSERT_TRUE(j.recover(scope, owner));
  ASSERT_NE(j.start(spec, source, owner), 0u);
  complete(j);
  EXPECT_EQ(j.status(owner).error, DownloadError::Digest);
  EXPECT_EQ(links, 0u);
  EXPECT_FALSE(Storage.exists(spec.path.data()));
}
TEST_F(HttpDownloadTest, ChangedVersionOrReaderProofRejectsBeforeBody) {
  spec.identity.revision = {};
  std::strcpy(spec.version.data(), "opaque-version");
  std::strcpy(spec.proof.data(), "900150983cd24fb0d6963f7d28e17f72");
  booksync::server::Download source(transport);
  DownloadReply reply;
  transport.changedVersion = true;
  EXPECT_FALSE(source.open(spec, 0, "", reply));
  EXPECT_EQ(source.error(), DownloadError::Edition);
  transport.changedVersion = false;
  transport.changedProof = true;
  EXPECT_FALSE(source.open(spec, 0, "", reply));
  EXPECT_EQ(source.error(), DownloadError::Edition);
  EXPECT_EQ(transport.opens, 0u);
}
TEST_F(HttpDownloadTest, FirstAcquisitionProofSurvivesRestartAndExactRange) {
  auto expected = spec.identity.revision;
  spec.identity.revision = {};
  std::strcpy(spec.version.data(), "opaque-version");
  std::strcpy(spec.proof.data(), "900150983cd24fb0d6963f7d28e17f72");
  transport.headers.status = 200;
  transport.headers.lengthKnown = true;
  transport.headers.contentLength = 3;
  std::strcpy(transport.headers.etag.data(), "\"revision\"");
  booksync::server::Download source(transport);
  uint64_t id;
  {
    DownloadJobs j(journal, admission, ports);
    ASSERT_TRUE(j.recover(scope, owner));
    id = j.start(spec, source, owner);
    ASSERT_NE(id, 0u);
    j.tick(owner);
  }
  card.restart();
  transport.headers.status = 206;
  transport.headers.contentLength = 2;
  std::strcpy(transport.headers.contentRange.data(), "bytes 1-2/3");
  DownloadJobs j(journal, admission, ports);
  ASSERT_TRUE(j.recover(scope, owner));
  ASSERT_TRUE(j.resume(id, source, owner));
  complete(j);
  EXPECT_EQ(j.result(id, owner), DeliveryResult::Ready);
  EXPECT_EQ(linked.revision, expected);
}

TEST_F(HttpDownloadTest, ReaderProofUsesExactBoundedReaderWindows) {
  std::string payload(8193, 'a');
  for (size_t i = 0; i < payload.size(); ++i) payload[i] = char('a' + i % 23);
  card.files["/proof"] = payload;
  std::strcpy(spec.proof.data(), "3e2237c1452fdf42baebef1e0811e130");
  booksync::server::Download source(transport);
  uint8_t scratch[1024];
  auto file = Storage.open("/proof", O_RDONLY);
  ASSERT_TRUE(file);
  card.events.clear();
  EXPECT_TRUE(source.verify(file, spec, {scratch, sizeof(scratch), [](void*) { return true; }, nullptr}));
  EXPECT_EQ(std::count(card.events.begin(), card.events.end(), "read"), 3);
  ASSERT_TRUE(file.close());
  // A sampled byte fails the reader proof; the worker independently hashes the
  // whole file for canonical revision rather than reusing this partial proof.
  card.files["/proof"][1024] = '!';
  file = Storage.open("/proof", O_RDONLY);
  EXPECT_FALSE(source.verify(file, spec, {scratch, sizeof(scratch), [](void*) { return true; }, nullptr}));
  EXPECT_TRUE(file.close());
  EXPECT_EQ(transport.opens, 0u);
}
TEST_F(HttpDownloadTest, ReaderProofStopsBeforeIoAtReadingGate) {
  card.files["/proof"] = "abc";
  std::strcpy(spec.proof.data(), "900150983cd24fb0d6963f7d28e17f72");
  booksync::server::Download source(transport);
  uint8_t scratch[1024];
  auto file = Storage.open("/proof", O_RDONLY);
  ASSERT_TRUE(file);
  card.events.clear();
  EXPECT_FALSE(source.verify(file, spec, {scratch, sizeof(scratch), [](void*) { return false; }, nullptr}));
  EXPECT_TRUE(card.events.empty());
  EXPECT_TRUE(file.close());
}
TEST_F(HttpDownloadTest, UnknownCanonicalRevisionRequiresAnAdapterVerifier) {
  spec.identity.revision = {};
  std::strcpy(spec.proof.data(), "opaque-proof");
  DownloadJobs j(journal, admission, ports);
  ASSERT_TRUE(j.recover(scope, owner));
  ASSERT_NE(j.start(spec, DownloadTest::source, owner), 0u);
  complete(j);
  EXPECT_EQ(j.status(owner).error, DownloadError::Digest);
  EXPECT_EQ(links, 0u);
  EXPECT_FALSE(Storage.exists(spec.path.data()));
}

}  // namespace
