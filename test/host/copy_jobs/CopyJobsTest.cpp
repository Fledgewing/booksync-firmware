// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <openssl/sha.h>

#include <cstring>

#include "booksync/services/CopyJobs.h"
using namespace booksync;
namespace {
constexpr storage::PublishPaths journal{"/copy", "/copy.tmp", "/copy.bak"};
constexpr DeliveryTicket owner{7, 1};
constexpr storage::ScopeView scope{"server", "account"};
storage::Revision revision(const char* text) {
  storage::Revision r;
  SHA256(reinterpret_cast<const unsigned char*>(text), std::strlen(text), r.bytes.data());
  return r;
}
struct CopyTest : testing::Test {
  unsigned parks = 0, restores = 0, stops = 0;
  Admission admission{{this, [](void*) { return MemorySnapshot{128 * 1024, 64 * 1024, 0}; },
                       [](void* p) {
                         ++static_cast<CopyTest*>(p)->parks;
                         return true;
                       },
                       [](void* p) {
                         ++static_cast<CopyTest*>(p)->restores;
                         return true;
                       },
                       [](void* p) {
                         ++static_cast<CopyTest*>(p)->stops;
                         return true;
                       }},
                      true};
  unsigned links = 0;
  bool linkOk = true, bookOpen = false;
  storage::SourceIdentity linked{};
  DownloadPorts ports{this, [](void*) { return DownloadJobs::Headroom + 10000; },
                      [](void* p, const DownloadSpec& selected) {
                        auto& t = *static_cast<CopyTest*>(p);
                        ++t.links;
                        t.linked = selected.identity;
                        return t.linkOk;
                      },
                      [](void* p, const char*) { return !static_cast<CopyTest*>(p)->bookOpen; }};
  DownloadSpec spec{};
  storage::BindingView binding{};
  struct Source : DownloadSource {
    unsigned opens = 0;
    size_t position = 0;
    bool bad = false;
    bool open(const DownloadSpec& s, uint64_t offset, const char*, DownloadReply& r) override {
      ++opens;
      position = offset;
      r.identity = s.identity;
      r.offset = offset;
      r.bytes = 3;
      r.sizeKnown = true;
      std::strcpy(r.validator.data(), "validator");
      return true;
    }
    int read(void* p, size_t) override {
      if (position == 3) return 0;
      *static_cast<char*>(p) = bad ? 'x' : "new"[position++];
      if (bad) ++position;
      return 1;
    }
    bool verify(FsFile& file, const DownloadSpec& selected, DownloadCheck scratch) override {
      return !std::strcmp(selected.proof.data(), "opaque-proof") && scratch.capacity >= 3 &&
             scratch.permitted(scratch.context) && file.seekSet(0) && file.read(scratch.bytes, 3) == 3 &&
             !std::memcmp(scratch.bytes, "new", 3);
    }
    bool finish() override { return true; }
    void close() override {}
  } source;
  void SetUp() override {
    card = {};
    links = 0;
    linked = {};
    source = Source{};
    linkOk = true;
    bookOpen = false;
    admission.setReading(false);
    std::strcpy(spec.server.data(), "server");
    std::strcpy(spec.account.data(), "account");
    std::strcpy(spec.book.data(), "book");
    std::strcpy(spec.file.data(), "file");
    std::strcpy(spec.path.data(), "/books/book.epub");
    spec.identity.book.bytes[0] = 1;
    spec.identity.edition.bytes[0] = 2;
    spec.identity.revision = revision("new");
    spec.bytes = 3;
    spec.sizeKnown = true;
    binding = {scope, {}, spec.path.data(), spec.identity};
    binding.source.revision = revision("old");
    card.files[spec.path.data()] = "old";
    card.files["/.crosspoint/epub_path/progress.bin"] = "native position and unknown fields";
    card.files["/.crosspoint/epub_path/bookmarks.bin"] = "bookmarks";
    card.files["/.crosspoint/epub_path/reader_settings.bin"] = "settings";
    card.files["/.crosspoint/clippings.json"] = "highlights";
    card.files["/.crosspoint/epub_path/stats.bin"] = "stats";
    card.disk = card.files;
  }
  void complete(CopyJobs& job) {
    for (unsigned n = 0; n < 20 && job.busy(); ++n) job.tick(owner);
  }
};
TEST_F(CopyTest, CurrentOldAmbiguousAndEditionConflict) {
  EXPECT_EQ(CopyJobs::classify(spec, &binding, 1), CopyState::Old);
  binding.source.revision = spec.identity.revision;
  EXPECT_EQ(CopyJobs::classify(spec, &binding, 1), CopyState::Current);
  auto conflict = binding;
  ++conflict.source.edition.bytes[0];
  storage::BindingView rows[]{binding, conflict};
  EXPECT_EQ(CopyJobs::classify(spec, rows, 2), CopyState::Ambiguous);
  EXPECT_EQ(CopyJobs::classify(spec, &conflict, 1), CopyState::Ambiguous);
  binding.scope.account = "foreign";
  EXPECT_EQ(CopyJobs::classify(spec, &binding, 1), CopyState::Missing);
}
TEST_F(CopyTest, ReplaceInPlacePreservesEverySidecarByteAndCommitsBackupRetirement) {
  auto before = card.files;
  CopyJobs job(journal, admission, ports);
  ASSERT_TRUE(job.recover(scope, owner));
  auto id = job.start(spec, &binding, 1, source, owner);
  ASSERT_NE(id, 0u);
  complete(job);
  EXPECT_EQ(job.result(id, owner), DeliveryResult::Ready);
  EXPECT_EQ(card.files[spec.path.data()], "new");
  EXPECT_FALSE(Storage.exists("/books/book.epub.bak"));
  for (const auto& [path, bytes] : before)
    if (path != spec.path.data()) EXPECT_EQ(card.files[path], bytes);
  EXPECT_EQ(links, 1u);
}
TEST_F(CopyTest, BadDigestKeepsOldAndReadingDoesNoWork) {
  CopyJobs job(journal, admission, ports);
  ASSERT_TRUE(job.recover(scope, owner));
  admission.setReading(true);
  auto events = card.events.size();
  EXPECT_EQ(job.start(spec, &binding, 1, source, owner), 0u);
  EXPECT_EQ(card.events.size(), events);
  admission.setReading(false);
  source.bad = true;
  auto id = job.start(spec, &binding, 1, source, owner);
  ASSERT_NE(id, 0u);
  complete(job);
  EXPECT_EQ(card.files[spec.path.data()], "old");
  EXPECT_EQ(links, 0u);
}
TEST_F(CopyTest, CurrentMissingAmbiguousAndUnverifiableNeverDownload) {
  CopyJobs job(journal, admission, ports);
  ASSERT_TRUE(job.recover(scope, owner));
  binding.source.revision = spec.identity.revision;
  EXPECT_EQ(job.start(spec, &binding, 1, source, owner), 0u);
  EXPECT_EQ(job.start(spec, nullptr, 0, source, owner), 0u);
  ++binding.source.edition.bytes[0];
  EXPECT_EQ(job.start(spec, &binding, 1, source, owner), 0u);
  binding.source = spec.identity;
  binding.source.revision = {};
  EXPECT_EQ(CopyJobs::classify(spec, &binding, 1), CopyState::Unverifiable);
  EXPECT_EQ(job.start(spec, &binding, 1, source, owner), 0u);
  EXPECT_EQ(source.opens, 0u);
}
TEST_F(CopyTest, ChangedLocalBytesAndUnownedBackupNeverOverwrite) {
  CopyJobs job(journal, admission, ports);
  ASSERT_TRUE(job.recover(scope, owner));
  card.files[spec.path.data()] = "external";
  auto id = job.start(spec, &binding, 1, source, owner);
  ASSERT_NE(id, 0u);
  complete(job);
  EXPECT_EQ(card.files[spec.path.data()], "external");
  EXPECT_EQ(source.opens, 0u);
}
TEST_F(CopyTest, RestartAfterPublishRetriesIdentityWithoutNetwork) {
  linkOk = false;
  uint64_t id;
  {
    CopyJobs job(journal, admission, ports);
    ASSERT_TRUE(job.recover(scope, owner));
    id = job.start(spec, &binding, 1, source, owner);
    complete(job);
    EXPECT_EQ(job.status(owner).state, DownloadState::Linking);
    EXPECT_EQ(card.files["/books/book.epub.bak"], "old");
  }
  card.restart();
  linkOk = true;
  auto opens = source.opens;
  CopyJobs job(journal, admission, ports);
  ASSERT_TRUE(job.recover(scope, owner));
  ASSERT_TRUE(job.resume(id, source, owner));
  complete(job);
  EXPECT_EQ(job.result(id, owner), DeliveryResult::Ready);
  EXPECT_EQ(source.opens, opens);
  EXPECT_FALSE(Storage.exists("/books/book.epub.bak"));
}
TEST_F(CopyTest, SecondUpdateRetiresOnlyOwnedBackup) {
  CopyJobs job(journal, admission, ports);
  ASSERT_TRUE(job.recover(scope, owner));
  auto id = job.start(spec, &binding, 1, source, owner);
  complete(job);
  ASSERT_EQ(job.result(id, owner), DeliveryResult::Ready);
  binding.source = spec.identity;
  spec.identity.revision = revision("xxx");
  source.bad = true;
  id = job.start(spec, &binding, 1, source, owner);
  ASSERT_NE(id, 0u);
  complete(job);
  EXPECT_EQ(job.result(id, owner), DeliveryResult::Ready);
  EXPECT_EQ(card.files[spec.path.data()], "xxx");
  EXPECT_FALSE(Storage.exists("/books/book.epub.bak"));
}
TEST_F(CopyTest, PublicationFailureRollsBackBeforeReportingFailure) {
  {
    CopyJobs job(journal, admission, ports);
    ASSERT_TRUE(job.recover(scope, owner));
    job.start(spec, &binding, 1, source, owner);
    complete(job);
  }
  const auto events = card.events;
  // Refuse the final staging->current rename, after old->backup succeeded.
  size_t rename = 0;
  for (size_t i = 0; i < events.size(); ++i)
    if (events[i] == "rename:before") rename = i;
  SetUp();
  card.fail = static_cast<int>(rename);
  CopyJobs job(journal, admission, ports);
  ASSERT_TRUE(job.recover(scope, owner));
  auto id = job.start(spec, &binding, 1, source, owner);
  complete(job);
  EXPECT_EQ(job.result(id, owner), DeliveryResult::Unavailable);
  EXPECT_EQ(card.files[spec.path.data()], "old");
  EXPECT_EQ(links, 0u);
}
TEST_F(CopyTest, PowerCutAtEveryBoundaryPreservesStateAndResumes) {
  {
    CopyJobs job(journal, admission, ports);
    ASSERT_TRUE(job.recover(scope, owner));
    job.start(spec, &binding, 1, source, owner);
    complete(job);
  }
  const int events = card.events.size();
  unsigned cuts = 0;
  for (bool eager : {false, true})
    for (int cut = 0; cut < events; ++cut) {
      SetUp();
      links = 0;
      card.eager = eager;
      card.stop = cut;
      const auto initial = card.files;
      {
        CopyJobs job(journal, admission, ports);
        try {
          if (job.recover(scope, owner)) {
            job.start(spec, &binding, 1, source, owner);
            complete(job);
          }
        } catch (const PowerCut&) {
        }
      }
      card.restart();
      CopyJobs job(journal, admission, ports);
      ASSERT_TRUE(job.recover(scope, owner)) << cut;
      ASSERT_TRUE(card.files[spec.path.data()] == "old" || card.files[spec.path.data()] == "new") << cut;
      for (const auto& [path, bytes] : initial)
        if (path != spec.path.data()) ASSERT_EQ(card.files[path], bytes) << cut;
      auto id = job.status(owner).id;
      if (id)
        ASSERT_TRUE(job.resume(id, source, owner)) << cut;
      else
        ASSERT_NE(job.start(spec, &binding, 1, source, owner), 0u) << cut;
      complete(job);
      ASSERT_EQ(job.status(owner).state, DownloadState::Complete) << cut;
      ASSERT_EQ(card.files[spec.path.data()], "new") << cut;
      ++cuts;
    }
  RecordProperty("replacement_power_cuts", cuts);
  EXPECT_GT(cuts, 100u);
}

TEST_F(CopyTest, MissingProofAndForeignScopeAreNotReplacementAuthority) {
  spec.identity.revision = {};
  EXPECT_EQ(CopyJobs::classify(spec, &binding, 1), CopyState::Unverifiable);
  std::strcpy(spec.proof.data(), "opaque-proof");
  EXPECT_EQ(CopyJobs::classify(spec, &binding, 1), CopyState::Old);
  binding.scope.server = "other";
  EXPECT_EQ(CopyJobs::classify(spec, &binding, 1), CopyState::Missing);
}
TEST_F(CopyTest, FullCardAndReadOnlyKeepOldAndSidecars) {
  for (bool full : {false, true}) {
    SetUp();
    const auto original = card.files;
    CopyJobs job(journal, admission, ports);
    ASSERT_TRUE(job.recover(scope, owner));
    card.full = full;
    card.readOnly = !full;
    auto id = job.start(spec, &binding, 1, source, owner);
    complete(job);
    EXPECT_EQ(job.result(id, owner), DeliveryResult::Unavailable);
    for (const auto& [path, bytes] : original) EXPECT_EQ(card.files[path], bytes);
    EXPECT_EQ(links, 0u);
  }
}
TEST_F(CopyTest, UnownedSiblingsAndAccountRetirementArePreserved) {
  card.files["/books/book.epub.bak"] = "unrelated";
  CopyJobs job(journal, admission, ports);
  ASSERT_TRUE(job.recover(scope, owner));
  EXPECT_EQ(job.start(spec, &binding, 1, source, owner), 0u);
  EXPECT_EQ(card.files["/books/book.epub.bak"], "unrelated");
  card.files.erase("/books/book.epub.bak");
  auto id = job.start(spec, &binding, 1, source, owner);
  ASSERT_NE(id, 0u);
  job.tick({7, 2});
  EXPECT_EQ(job.result(id, {7, 2}), DeliveryResult::Canceled);
  EXPECT_EQ(card.files[spec.path.data()], "old");
}
TEST_F(CopyTest, InterruptedRenameRecoveryIsGatedAndRecoveryCutsAreSafe) {
  // Seed an owned transaction, then model loss after old->backup and before
  // staging->current. Recovery must restore the exact native path first.
  {
    CopyJobs job(journal, admission, ports);
    ASSERT_TRUE(job.recover(scope, owner));
    job.start(spec, &binding, 1, source, owner);
    job.tick(owner);
  }
  card.files["/books/book.epub.bak"] = card.files[spec.path.data()];
  card.files.erase(spec.path.data());
  card.disk = card.files;
  const auto retained = card.disk;
  admission.setReading(true);
  {
    CopyJobs job(journal, admission, ports);
    EXPECT_FALSE(job.recover(scope, owner));
  }
  EXPECT_FALSE(Storage.exists(spec.path.data()));
  admission.setReading(false);
  card.events.clear();
  {
    CopyJobs job(journal, admission, ports);
    ASSERT_TRUE(job.recover(scope, owner));
  }
  const int events = card.events.size();
  RecordProperty("recovery_power_cuts", events * 2);
  for (bool eager : {false, true})
    for (int cut = 0; cut < events; ++cut) {
      card = {};
      card.files = card.disk = retained;
      card.eager = eager;
      card.stop = cut;
      {
        CopyJobs job(journal, admission, ports);
        try {
          job.recover(scope, owner);
        } catch (const PowerCut&) {
        }
      }
      card.restart();
      CopyJobs job(journal, admission, ports);
      ASSERT_TRUE(job.recover(scope, owner)) << cut;
      EXPECT_EQ(card.files[spec.path.data()], "old") << cut;
    }
}
TEST_F(CopyTest, VersionTwoDownloadJournalRemainsReadable) {
  // F23's v2 has the same exact fields except the final old-revision bytes.
  {
    DownloadJobs job(journal, admission, ports);
    ASSERT_TRUE(job.recover(scope, owner));
    std::strcpy(spec.path.data(), "/books/first.epub");
    job.start(spec, source, owner);
    job.tick(owner);
  }
  auto& bytes = card.files[journal.current];
  ASSERT_EQ(bytes.size(), 943u);
  bytes.erase(907, 32);
  bytes[4] = 2;
  uint32_t sum = 2166136261u;
  for (size_t i = 0; i < bytes.size() - 4; ++i) sum = (sum ^ uint8_t(bytes[i])) * 16777619u;
  for (unsigned i = 0; i < 4; ++i) bytes[bytes.size() - 4 + i] = char(sum >> (8 * i));
  DownloadJobs job(journal, admission, ports);
  ASSERT_TRUE(job.recover(scope, owner));
  ASSERT_TRUE(job.resume(job.status(owner).id, source, owner));
  for (unsigned i = 0; i < 20 && job.busy(); ++i) job.tick(owner);
  EXPECT_EQ(job.status(owner).state, DownloadState::Complete);
}

TEST_F(CopyTest, ExistingUppercaseEpubKeepsItsExactPath) {
  std::strcpy(spec.path.data(), "/books/Book.EPUB");
  binding.path = spec.path.data();
  card.files[spec.path.data()] = "old";
  CopyJobs job(journal, admission, ports);
  ASSERT_TRUE(job.recover(scope, owner));
  auto id = job.start(spec, &binding, 1, source, owner);
  ASSERT_NE(id, 0u);
  complete(job);
  EXPECT_EQ(job.result(id, owner), DeliveryResult::Ready);
  EXPECT_EQ(card.files[spec.path.data()], "new");
  EXPECT_EQ(card.files["/books/book.epub"], "old");
}

TEST_F(CopyTest, AdapterProofReplacementIssuesCanonicalShaAndRetriesLinkAfterRestart) {
  spec.identity.revision = {};
  std::strcpy(spec.proof.data(), "opaque-proof");
  linkOk = false;
  uint64_t id;
  {
    CopyJobs job(journal, admission, ports);
    ASSERT_TRUE(job.recover(scope, owner));
    id = job.start(spec, &binding, 1, source, owner);
    ASSERT_NE(id, 0u);
    complete(job);
    ASSERT_EQ(job.status(owner).state, DownloadState::Linking);
    EXPECT_EQ(linked.revision, revision("new"));
  }
  card.restart();
  linkOk = true;
  const auto opens = source.opens;
  CopyJobs job(journal, admission, ports);
  ASSERT_TRUE(job.recover(scope, owner));
  ASSERT_TRUE(job.resume(id, source, owner));
  complete(job);
  EXPECT_EQ(job.result(id, owner), DeliveryResult::Ready);
  EXPECT_EQ(source.opens, opens);
  EXPECT_EQ(linked.revision, revision("new"));
}

TEST_F(CopyTest, FilenameRecoveryDoesNotActivateNetworkOrParkDisplay) {
  {
    CopyJobs job(journal, admission, ports);
    ASSERT_TRUE(job.recover(scope, owner));
    job.start(spec, &binding, 1, source, owner);
    job.tick(owner);
  }
  card.files["/books/book.epub.bak"] = card.files[spec.path.data()];
  card.files.erase(spec.path.data());
  card.disk = card.files;
  CopyJobs job(journal, admission, ports);
  bookOpen = true;
  EXPECT_FALSE(job.recover(scope, owner));
  EXPECT_FALSE(Storage.exists(spec.path.data()));
  bookOpen = false;
  ASSERT_EQ(admission.begin(TAILNET_JOB_CATALOG), AdmissionResult::Admitted);
  EXPECT_FALSE(job.recover(scope, owner));
  EXPECT_TRUE(admission.busy());
  EXPECT_FALSE(Storage.exists(spec.path.data()));
  ASSERT_TRUE(admission.finish());
  const auto prepared = tailnet_service_prepare(TAILNET_JOB_CATALOG);
  ASSERT_NE(prepared.epoch, 0u);
  EXPECT_FALSE(job.recover(scope, owner));
  EXPECT_FALSE(Storage.exists(spec.path.data()));
  tailnet_service_release(prepared);
  parks = restores = stops = 0;
  ASSERT_TRUE(job.recover(scope, owner));
  EXPECT_EQ(card.files[spec.path.data()], "old");
  EXPECT_EQ(parks, 0u);
  EXPECT_EQ(restores, 0u);
  EXPECT_EQ(stops, 0u);
  EXPECT_FALSE(admission.busy());
  EXPECT_FALSE(tailnet_service_permitted());
}

TEST_F(CopyTest, LoadedBookBeneathHomeAndLateOpenAreNeverReplaced) {
  // Missing lifetime wiring cannot opt into destructive replacement.
  {
    auto missing = ports;
    missing.replacementClosed = nullptr;
    CopyJobs unwired(journal, admission, missing);
    ASSERT_TRUE(unwired.recover(scope, owner));
    EXPECT_EQ(unwired.start(spec, &binding, 1, source, owner), 0u);
    EXPECT_EQ(source.opens, 0u);
  }
  // Foreground is Home (network admission allowed), but the document is loaded.
  bookOpen = true;
  CopyJobs job(journal, admission, ports);
  ASSERT_TRUE(job.recover(scope, owner));
  const auto events = card.events.size();
  EXPECT_EQ(job.start(spec, &binding, 1, source, owner), 0u);
  EXPECT_EQ(card.events.size(), events);
  EXPECT_EQ(source.opens, 0u);
  bookOpen = false;
  const auto id = job.start(spec, &binding, 1, source, owner);
  ASSERT_NE(id, 0u);
  job.tick(owner);
  bookOpen = true;
  job.tick(owner);
  EXPECT_EQ(job.status(owner).state, DownloadState::Paused);
  EXPECT_EQ(card.files[spec.path.data()], "old");
  EXPECT_EQ(links, 0u);
  EXPECT_FALSE(job.resume(id, source, owner));
  bookOpen = false;
  ASSERT_TRUE(job.resume(id, source, owner));
  complete(job);
  EXPECT_EQ(job.result(id, owner), DeliveryResult::Ready);
}

TEST_F(CopyTest, MultipleBooksThenAnotherRevisionDoNotOrphanRollbackFiles) {
  const auto first = spec;
  const auto firstBinding = binding;
  CopyJobs job(journal, admission, ports);
  ASSERT_TRUE(job.recover(scope, owner));
  auto id = job.start(spec, &binding, 1, source, owner);
  complete(job);
  ASSERT_EQ(job.result(id, owner), DeliveryResult::Ready);
  std::strcpy(spec.path.data(), "/books/second.epub");
  std::strcpy(spec.book.data(), "second-book");
  ++spec.identity.book.bytes[0];
  binding = {scope, {}, spec.path.data(), spec.identity};
  binding.source.revision = revision("old");
  card.files[spec.path.data()] = "old";
  id = job.start(spec, &binding, 1, source, owner);
  ASSERT_NE(id, 0u);
  complete(job);
  ASSERT_EQ(job.result(id, owner), DeliveryResult::Ready);
  spec = first;
  binding = firstBinding;
  binding.source.revision = first.identity.revision;
  spec.identity.revision = revision("xxx");
  source.bad = true;
  id = job.start(spec, &binding, 1, source, owner);
  ASSERT_NE(id, 0u);
  complete(job);
  EXPECT_EQ(job.result(id, owner), DeliveryResult::Ready);
  EXPECT_EQ(card.files[spec.path.data()], "xxx");
  EXPECT_EQ(card.files["/books/second.epub"], "new");
  EXPECT_FALSE(Storage.exists("/books/book.epub.bak"));
  EXPECT_FALSE(Storage.exists("/books/second.epub.bak"));
}

}  // namespace
