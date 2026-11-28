// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include <cstring>

#include "booksync/services/CatalogJobs.h"
using namespace booksync;
constexpr storage::ScopeView StableScope{"fixture-server-token", "fixture-account-token"};
constexpr storage::ScopeView OtherScope{"fixture-server-token", "foreign-account-token"};
namespace {
struct CatalogJobsFixture : testing::Test {
  storage::ProjectionStore store{7, 1, StableScope};
  ReadModel model{store.index()};
  bool stopped = true;
  unsigned starts = 0, canceled = 0, rows = 0, total = 20;
  CatalogError fault = CatalogError::None;
  MemorySnapshot memory{128 * 1024, 64 * 1024, 0};
  Admission admission{
      {this, [](void* p) { return static_cast<CatalogJobsFixture*>(p)->memory; }, [](void*) { return true; },
       [](void*) { return true; }, [](void* p) { return static_cast<CatalogJobsFixture*>(p)->stopped; }},
      true};
  struct Source : CatalogSource {
    CatalogJobsFixture& f;
    explicit Source(CatalogJobsFixture& f) : f(f) {}
    CatalogStep next(CatalogRow& row) override {
      if (f.rows++ < f.total) {
        row = {};
        std::strcpy(row.title.data(), "Saved book");
        std::strcpy(row.alias.data(), "opaque/book:key");
        return CatalogStep::Record;
      }
      return f.fault == CatalogError::None ? CatalogStep::End : CatalogStep::Error;
    }
    CatalogError error() const override { return f.fault; }
    void cancel() override { ++f.canceled; }
  } source{*this};
  CatalogJobs jobs{store, model, admission};
  static bool bind(void*, const CatalogRow& row, storage::IndexRecord& out) {
    std::strcpy(out.path.data(), "/fixture.epub");
    out.title = row.title;
    return true;
  }
  CatalogRequest request() {
    return {&source,
            {[](void* p) {
               auto& f = *static_cast<CatalogJobsFixture*>(p);
               ++f.starts;
               f.rows = 0;
               return true;
             },
             this},
            bind,
            nullptr,
            {7, 1},
            StableScope};
  }
  void SetUp() override {
    card = {};
    admission.setReading(false);
    ASSERT_TRUE(store.recover());
  }
  void finish(uint64_t run) {
    while (jobs.busy()) jobs.tick({7, 1});
    EXPECT_EQ(jobs.result(run, {7, 1}), DeliveryResult::Ready);
  }
};
TEST_F(CatalogJobsFixture, CoalescesAndCommitsOnlyAtEnd) {
  const auto run = jobs.start(request(), {7, 1});
  ASSERT_NE(run, 0u);
  EXPECT_EQ(jobs.start(request(), {7, 1}), run);
  EXPECT_EQ(starts, 1u);
  EXPECT_TRUE(jobs.tick({7, 1}));
  EXPECT_FALSE(store.index().ready());
  finish(run);
  EXPECT_EQ(store.index().count(), 20u);
  ASSERT_TRUE(model.materialize({}));
  EXPECT_EQ(model.window().count, 12u);
  EXPECT_FALSE(admission.busy());
}
TEST_F(CatalogJobsFixture, FailedAndCanceledGenerationsPreserveGoodData) {
  finish(jobs.start(request(), {7, 1}));
  fault = CatalogError::PermissionLost;
  auto run = jobs.start(request(), {7, 1});
  while (jobs.busy()) jobs.tick({7, 1});
  EXPECT_EQ(jobs.result(run, {7, 1}), DeliveryResult::Unavailable);
  EXPECT_EQ(store.index().provenance().generation, 1u);
  fault = CatalogError::None;
  run = jobs.start(request(), {7, 1});
  ASSERT_NE(run, 0u);
  jobs.tick({7, 1});
  jobs.cancel(run);
  while (jobs.busy()) jobs.tick({7, 1});
  EXPECT_EQ(jobs.result(run, {7, 1}), DeliveryResult::Canceled);
  EXPECT_EQ(store.index().provenance().generation, 1u);
}
TEST_F(CatalogJobsFixture, ScopeMismatchAndAccountSwitchNeverPublish) {
  EXPECT_EQ(jobs.start(request(), {8, 1}), 0u);
  EXPECT_EQ(starts, 0u);
  auto run = jobs.start(request(), {7, 1});
  ASSERT_NE(run, 0u);
  jobs.tick({7, 2});
  EXPECT_FALSE(store.index().ready());
  EXPECT_EQ(jobs.result(run, {7, 2}), DeliveryResult::Canceled);
  storage::ProjectionStore other(8, 1, OtherScope);
  ASSERT_TRUE(other.recover());
  EXPECT_FALSE(other.index().ready());
  storage::ProjectionStore later(7, 2, StableScope);
  ASSERT_TRUE(later.recover());
  EXPECT_FALSE(later.index().ready());
}
TEST_F(CatalogJobsFixture, OldCancelCannotReleaseNewLease) {
  auto old = jobs.start(request(), {7, 1});
  finish(old);
  auto next = jobs.start(request(), {7, 1});
  ASSERT_NE(next, old);
  jobs.cancel(old);
  EXPECT_TRUE(jobs.busy());
  EXPECT_TRUE(admission.permitted());
  finish(next);
}
TEST_F(CatalogJobsFixture, AdmissionRefusalDoesNotStartTransport) {
  admission.setReading(true);
  EXPECT_EQ(jobs.start(request(), {7, 1}), 0u);
  EXPECT_EQ(starts, 0u);
  admission.setReading(false);
  memory.internalLargest = 1024;
  EXPECT_EQ(jobs.start(request(), {7, 1}), 0u);
  EXPECT_EQ(starts, 0u);
  EXPECT_FALSE(admission.busy());
}
TEST_F(CatalogJobsFixture, RetiredHomeAndTimedOutDeliveryDoNotCancelWorkOrRepaint) {
  unsigned paints = 0;
  Delivery delivery({7, 1}, 0, DeliveryPolicy::Owner,
                    {[](void* p, DeliveryResult) { ++*static_cast<unsigned*>(p); }, &paints});
  auto run = jobs.start(request(), {7, 1});
  delivery.retire();
  finish(run);
  delivery.poll(200000, jobs.result(run, {7, 1}), {7, 1});
  EXPECT_EQ(paints, 0u);
  Delivery late({7, 1}, 0, DeliveryPolicy::Owner,
                {[](void* p, DeliveryResult) { ++*static_cast<unsigned*>(p); }, &paints});
  run = jobs.start(request(), {7, 1});
  late.poll(120000, jobs.result(run, {7, 1}), {7, 1});
  EXPECT_EQ(paints, 1u);
  finish(run);
  late.poll(120001, jobs.result(run, {7, 1}), {7, 1});
  EXPECT_EQ(paints, 1u);
}
TEST_F(CatalogJobsFixture, TeardownRetriesBeforeNewRunAdmission) {
  auto run = jobs.start(request(), {7, 1});
  stopped = false;
  jobs.cancel(run);
  EXPECT_TRUE(jobs.busy());
  EXPECT_EQ(jobs.start(request(), {7, 1}), 0u);
  stopped = true;
  jobs.tick({7, 1});
  EXPECT_FALSE(jobs.busy());
}
TEST_F(CatalogJobsFixture, ReadingRevokesActiveRefreshWithoutPublishing) {
  auto run = jobs.start(request(), {7, 1});
  jobs.tick({7, 1});
  admission.setReading(true);
  jobs.tick({7, 1});
  EXPECT_FALSE(store.index().ready());
  EXPECT_EQ(jobs.result(run, {7, 1}), DeliveryResult::Canceled);
}
TEST_F(CatalogJobsFixture, RecoverRetainsOpaqueRecordsAndRejectsStaleKeys) {
  finish(jobs.start(request(), {7, 1}));
  storage::ProjectionStore recovered(7, 1, StableScope);
  ASSERT_TRUE(recovered.recover());
  static CatalogRow row;
  ASSERT_TRUE(recovered.read({recovered.index().provenance(), 0}, row));
  EXPECT_STREQ(row.alias.data(), "opaque/book:key");
  EXPECT_STREQ(row.title.data(), "Saved book");
  auto key = BookKey{recovered.index().provenance(), 0};
  key.provenance.owner = 8;
  EXPECT_FALSE(recovered.read(key, row));
  const auto raw = std::find_if(card.files.begin(), card.files.end(),
                                [](const auto& item) { return item.first.ends_with(".rows-b"); });
  ASSERT_NE(raw, card.files.end());
  raw->second[288] ^= 1;
  EXPECT_FALSE(recovered.read({recovered.index().provenance(), 0}, row));
}
TEST_F(CatalogJobsFixture, FullOrReadOnlyStorageNeverReplacesSavedGeneration) {
  finish(jobs.start(request(), {7, 1}));
  card.full = true;
  EXPECT_EQ(jobs.start(request(), {7, 1}), 0u);
  card.full = false;
  card.readOnly = true;
  EXPECT_EQ(jobs.start(request(), {7, 1}), 0u);
  card.readOnly = false;
  ASSERT_TRUE(store.recover());
  EXPECT_EQ(store.index().provenance().generation, 1u);
}
TEST_F(CatalogJobsFixture, EveryStorageBoundaryCutSelectsACompleteGeneration) {
  total = 1;
  finish(jobs.start(request(), {7, 1}));
  const auto baseline = card.disk;
  card.events.clear();
  finish(jobs.start(request(), {7, 1}));
  const auto boundaries = card.events.size();
  ASSERT_GT(boundaries, 100u);
  std::cout << "F21 cut boundaries: " << boundaries << " x 2 persistence models\n";
  for (bool eager : {false, true})
    for (size_t cut = 0; cut < boundaries; ++cut) {
      card = {};
      card.disk = baseline;
      card.files = baseline;
      card.eager = eager;
      {
        storage::ProjectionStore s(7, 1, StableScope);
        ReadModel m(s.index());
        CatalogJobs j(s, m, admission);
        card.stop = cut;
        try {
          auto run = j.start(request(), {7, 1});
          while (run && j.busy()) j.tick({7, 1});
        } catch (const PowerCut&) {
        }
        // A real reset does not run destructors; stop injecting before host cleanup.
        card.stop = -1;
      }
      card.restart();
      storage::ProjectionStore s(7, 1, StableScope);
      ASSERT_TRUE(s.recover()) << cut;
      ASSERT_TRUE(s.index().ready()) << cut;
      EXPECT_EQ(s.index().count(), 1u) << cut;
      EXPECT_TRUE(s.index().provenance().generation == 1 || s.index().provenance().generation == 2) << cut;
      static CatalogRow row;
      ASSERT_TRUE(s.read({s.index().provenance(), 0}, row)) << cut;
      EXPECT_STREQ(row.alias.data(), "opaque/book:key");
    }
}
TEST_F(CatalogJobsFixture, NewServiceLifetimeNeverReusesAnOldLease) {
  auto old = jobs.start(request(), {7, 1});
  finish(old);
  CatalogJobs next(store, model, admission);
  auto run = next.start(request(), {7, 1});
  ASSERT_NE(run, old);
  next.cancel(old);
  EXPECT_TRUE(next.busy());
  while (next.busy()) next.tick({7, 1});
  EXPECT_EQ(next.result(run, {7, 1}), DeliveryResult::Ready);
}
}  // namespace

TEST(ProjectionStore, MissingDurableScopeIsRejectedBeforeAnyIO) {
  card = {};
  booksync::storage::ProjectionStore store(7, 1, {});
  EXPECT_FALSE(store.recover());
  EXPECT_FALSE(store.begin());
  EXPECT_TRUE(card.events.empty());
}
TEST(ProjectionStore, FailedAppendPoisonsTheWholeTransaction) {
  card = {};
  booksync::storage::ProjectionStore store(7, 1, StableScope);
  ASSERT_TRUE(store.begin());
  static booksync::CatalogRow row;
  static booksync::storage::IndexRecord record;
  row.title.fill('x');  // Missing terminator must never publish a successful empty catalog.
  EXPECT_FALSE(store.append(row, record));
  EXPECT_FALSE(store.commit());
  ASSERT_TRUE(store.recover());
  EXPECT_FALSE(store.index().ready());
  row = {};
}
TEST_F(CatalogJobsFixture, UnresolvedBindingAndStartFailureReleaseAllResources) {
  auto req = request();
  req.bind = [](void*, const CatalogRow&, storage::IndexRecord&) { return false; };
  const auto run = jobs.start(req, {7, 1});
  ASSERT_NE(run, 0u);
  EXPECT_FALSE(jobs.tick({7, 1}));
  EXPECT_EQ(jobs.result(run, {7, 1}), DeliveryResult::Unavailable);
  EXPECT_FALSE(admission.busy());
  req = request();
  req.start.call = [](void*) { return false; };
  EXPECT_EQ(jobs.start(req, {7, 1}), 0u);
  EXPECT_FALSE(admission.busy());
  EXPECT_FALSE(store.index().ready());
}
TEST_F(CatalogJobsFixture, ForeignAdmissionAndReturnedAccountCannotReviveRefresh) {
  ASSERT_EQ(admission.begin(TAILNET_JOB_TRANSFER), AdmissionResult::Admitted);
  EXPECT_EQ(jobs.start(request(), {7, 1}), 0u);
  EXPECT_TRUE(admission.permitted());
  ASSERT_TRUE(admission.finish());
  const auto run = jobs.start(request(), {7, 1});
  jobs.tick({8, 2});
  EXPECT_EQ(jobs.result(run, {7, 3}), DeliveryResult::Canceled);
  EXPECT_FALSE(store.index().ready());
}
TEST(ProjectionStore, AllNeutralFieldsRoundTripWithoutPerRecordWorkspace) {
  card = {};
  booksync::storage::ProjectionStore store(7, 1, StableScope);
  ASSERT_TRUE(store.begin());
  static booksync::CatalogRow row, out;
  row = {};
  std::strcpy(row.alias.data(), "opaque/book");
  std::strcpy(row.title.data(), "Synthetic title");
  row.authorCount = 1;
  row.formatCount = 1;
  row.fileCount = 1;
  std::strcpy(row.authors[0].data(), "Synthetic author");
  std::strcpy(row.formats[0].data(), "epub");
  std::strcpy(row.seriesAlias.data(), "opaque/series");
  std::strcpy(row.series.data(), "Synthetic series");
  std::strcpy(row.sequence.data(), "2.5");
  std::strcpy(row.added.data(), "2026-11-27");
  row.reading = booksync::CatalogReading::InProgress;
  row.fraction = .375;
  row.fractionKnown = true;
  row.artworkKnown = true;
  row.artworkPresent = true;
  row.titleTruncated = true;
  auto& f = row.files[0];
  std::strcpy(f.alias.data(), "opaque/file");
  std::strcpy(f.version.data(), "revision-v2");
  std::strcpy(f.rendition.data(), "text-only");
  std::strcpy(f.selection.data(), "opaque-selection");
  f.bytes = (uint64_t{1} << 40) + 7;
  f.sizeKnown = true;
  f.sourceAudio = true;
  static booksync::storage::IndexRecord record;
  std::strcpy(record.path.data(), "/fixture.epub");
  record.title = row.title;
  ASSERT_TRUE(store.append(row, record));
  ASSERT_TRUE(store.commit());
  ASSERT_TRUE(store.recover());
  ASSERT_TRUE(store.read({store.index().provenance(), 0}, out));
  EXPECT_EQ(out.fraction, row.fraction);
  EXPECT_EQ(out.reading, row.reading);
  EXPECT_TRUE(out.fractionKnown);
  EXPECT_TRUE(out.artworkKnown);
  EXPECT_TRUE(out.artworkPresent);
  EXPECT_TRUE(out.titleTruncated);
  EXPECT_STREQ(out.authors[0].data(), row.authors[0].data());
  EXPECT_STREQ(out.seriesAlias.data(), row.seriesAlias.data());
  EXPECT_EQ(out.files[0].bytes, f.bytes);
  EXPECT_TRUE(out.files[0].sizeKnown);
  EXPECT_TRUE(out.files[0].sourceAudio);
  EXPECT_STREQ(out.files[0].selection.data(), f.selection.data());
}
TEST_F(CatalogJobsFixture, AccountTransitionClearsPublishedRowsEvenWithoutActiveRun) {
  finish(jobs.start(request(), {7, 1}));
  ASSERT_TRUE(model.materialize({}));
  ASSERT_EQ(model.availability(), Availability::Ready);
  jobs.tick({8, 2});
  EXPECT_EQ(model.availability(), Availability::NotSaved);
  EXPECT_EQ(model.window().count, 0);
  EXPECT_EQ(model.window().windowCount, 0);
  EXPECT_EQ(model.window().total, 0u);
  EXPECT_EQ(model.window().provenance.owner, 0u);
}
TEST_F(CatalogJobsFixture, AccountClearsSnapshotBeforeSlowNetworkDrainCompletes) {
  finish(jobs.start(request(), {7, 1}));
  ASSERT_TRUE(model.materialize({}));
  auto run = jobs.start(request(), {7, 1});
  stopped = false;
  jobs.cancel(run);
  EXPECT_TRUE(jobs.busy());
  jobs.tick({8, 2});
  EXPECT_TRUE(jobs.busy());
  EXPECT_EQ(model.window().count, 0);
  EXPECT_EQ(model.window().total, 0u);
  stopped = true;
  jobs.tick({8, 2});
  EXPECT_FALSE(jobs.busy());
}
TEST_F(CatalogJobsFixture, CompleteEmptyCatalogIsReadyButRevocationIsNotEmptySuccess) {
  total = 0;
  fault = CatalogError::PermissionLost;
  auto run = jobs.start(request(), {7, 1});
  EXPECT_FALSE(jobs.tick({7, 1}));
  EXPECT_FALSE(store.index().ready());
  EXPECT_EQ(jobs.result(run, {7, 1}), DeliveryResult::Unavailable);
  fault = CatalogError::None;
  finish(jobs.start(request(), {7, 1}));
  ASSERT_TRUE(store.recover());
  ASSERT_TRUE(model.materialize({}));
  EXPECT_EQ(model.availability(), Availability::Ready);
  EXPECT_EQ(model.window().total, 0u);
}
TEST_F(CatalogJobsFixture, CapturedSessionScopeCannotBeRelabeledAtDispatch) {
  auto req = request();
  req.owner = {8, 1};
  EXPECT_EQ(jobs.start(req, {7, 1}), 0u);
  EXPECT_EQ(starts, 0u);
  req.owner = {7, 2};
  EXPECT_EQ(jobs.start(req, {7, 1}), 0u);
  EXPECT_EQ(starts, 0u);
}
TEST_F(CatalogJobsFixture, ServerOnlyCatalogPersistsWithoutInventedIdentityOrPath) {
  auto req = request();
  req.bind = nullptr;
  finish(jobs.start(req, {7, 1}));
  ASSERT_TRUE(store.recover());
  ASSERT_TRUE(model.materialize({}));
  EXPECT_EQ(model.window().count, 12);
  EXPECT_FALSE(model.window().rows[0].identityKnown);
  static storage::IndexRecord record;
  ASSERT_TRUE(store.index().read({store.index().provenance(), 0}, record));
  EXPECT_EQ(record.path[0], 0);
  static CatalogRow row;
  ASSERT_TRUE(store.read({store.index().provenance(), 0}, row));
  EXPECT_STREQ(row.alias.data(), "opaque/book:key");
}
TEST_F(CatalogJobsFixture, DeviceIndexCannotReadCatalogV2AsLocalPathRecords) {
  auto req = request();
  req.bind = nullptr;
  finish(jobs.start(req, {7, 1}));
  char hash[17]{};
  std::snprintf(hash, sizeof(hash), "%016llx", static_cast<unsigned long long>(store.index().provenance().owner));
  const auto prefix = std::string("/.booksync/catalog-") + hash;
  const auto current = prefix + ".index", temp = prefix + ".index.tmp", backup = prefix + ".index.bak",
             dataA = prefix + ".a", dataB = prefix + ".b";
  storage::Index local({current.c_str(), temp.c_str(), backup.c_str()}, dataA.c_str(), dataB.c_str());
  EXPECT_FALSE(local.recover());
  EXPECT_FALSE(local.ready());
  ASSERT_TRUE(store.recover());
  EXPECT_EQ(store.index().count(), 20u);
}
TEST_F(CatalogJobsFixture, ForeignGenerationRenamedIntoScopeCannotLeaveReadableRows) {
  finish(jobs.start(request(), {7, 1}));
  const auto saved = card.files;
  char a[17]{}, b[17]{};
  std::snprintf(a, sizeof(a), "%016llx", static_cast<unsigned long long>(store.index().provenance().owner));
  storage::ProjectionStore other(7, 1, OtherScope);
  ReadModel otherModel(other.index());
  CatalogJobs otherJobs(other, otherModel, admission);
  auto req = request();
  req.scope = OtherScope;
  auto run = otherJobs.start(req, {7, 1});
  ASSERT_NE(run, 0u);
  while (otherJobs.busy()) otherJobs.tick({7, 1});
  std::snprintf(b, sizeof(b), "%016llx", static_cast<unsigned long long>(other.index().provenance().owner));
  const auto namespaceId = other.index().provenance().owner;
  const auto put = [](std::string& bytes, size_t at, uint64_t value, size_t count) {
    for (size_t i = 0; i < count; ++i) bytes[at + i] = static_cast<char>(value >> (8 * i));
  };
  const auto checksum = [](const std::string& bytes, size_t count) {
    uint32_t sum = 2166136261u;
    for (size_t i = 0; i < count; ++i) sum = (sum ^ static_cast<uint8_t>(bytes[i])) * 16777619u;
    return sum;
  };
  // Simulate a bucket collision with valid envelope checksums. Only exact
  // embedded F08 tokens may establish scope; a matching numeric bucket cannot.
  for (const auto& [name, original] : saved) {
    auto foreign = name, bytes = original;
    const auto at = foreign.find(a);
    ASSERT_NE(at, std::string::npos);
    foreign.replace(at, 16, b);
    if (name.ends_with(".index")) {
      put(bytes, 16, namespaceId, 8);
      put(bytes, 44, checksum(bytes, 44), 4);
    } else if (name.ends_with(".a") || name.ends_with(".b")) {
      put(bytes, 12, namespaceId, 8);
      put(bytes, 36, checksum(bytes, 36), 4);
    } else if (name.ends_with(".rows-a") || name.ends_with(".rows-b")) {
      put(bytes, 4, namespaceId, 8);
      put(bytes, 284, checksum(bytes, 284), 4);
    }
    card.files[foreign] = bytes;
  }
  EXPECT_FALSE(other.recover());
  EXPECT_FALSE(other.index().ready());
  EXPECT_EQ(other.index().count(), 0u);
  const auto retained = card.files;
  EXPECT_FALSE(other.begin());
  EXPECT_EQ(card.files, retained);
  ReadModel isolated(other.index());
  ASSERT_TRUE(isolated.materialize({}));
  EXPECT_EQ(isolated.availability(), Availability::NotSaved);
  EXPECT_EQ(isolated.window().count, 0);
  static CatalogRow row;
  EXPECT_FALSE(other.read({other.index().provenance(), 0}, row));
}

TEST_F(CatalogJobsFixture, RebootIdentityReuseCannotAdoptAnotherAccountsCatalog) {
  finish(jobs.start(request(), {7, 1}));
  storage::ProjectionStore foreign(7, 1, OtherScope);
  ASSERT_TRUE(foreign.recover());
  EXPECT_FALSE(foreign.index().ready());
  // A fresh process can reuse a ticket number for a different account. Stable
  // F08 scope, rather than that process-local number, owns the durable bytes.
  storage::ProjectionStore reopened(8, 3, StableScope);
  ASSERT_TRUE(reopened.recover());
  EXPECT_EQ(reopened.index().count(), 20u);
  EXPECT_TRUE(reopened.matches({8, 3}));
  EXPECT_FALSE(reopened.matches({7, 1}));
  static CatalogRow row;
  ASSERT_TRUE(reopened.read({reopened.index().provenance(), 0}, row));
  EXPECT_STREQ(row.alias.data(), "opaque/book:key");
}
TEST_F(CatalogJobsFixture, StableScopeSeparatesServerInstancesAsWellAsAccounts) {
  finish(jobs.start(request(), {7, 1}));
  storage::ProjectionStore foreign(7, 1, {"other-server-token", "fixture-account-token"});
  ASSERT_TRUE(foreign.recover());
  EXPECT_FALSE(foreign.index().ready());
}

TEST_F(CatalogJobsFixture, CapturedStableScopeCannotCrossAnOtherwiseMatchingTicket) {
  auto req = request();
  req.scope = OtherScope;
  EXPECT_EQ(jobs.start(req, {7, 1}), 0u);
  EXPECT_EQ(starts, 0u);
  EXPECT_FALSE(store.index().ready());
}
TEST(ProjectionStore, FullWidthProcessEpochIsNotTruncatedIntoDurableIdentity) {
  card = {};
  storage::ProjectionStore store(7, uint64_t{1} << 40, StableScope);
  EXPECT_TRUE(store.matches({7, uint64_t{1} << 40}));
  EXPECT_FALSE(store.matches({7, 0}));
  ASSERT_TRUE(store.begin());
  ASSERT_TRUE(store.commit());
  storage::ProjectionStore reopened(8, 1, StableScope);
  ASSERT_TRUE(reopened.recover());
  EXPECT_TRUE(reopened.index().ready());
}
