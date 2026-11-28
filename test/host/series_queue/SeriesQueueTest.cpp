// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "booksync/services/SeriesQueue.h"
using namespace booksync;
namespace {
constexpr storage::PublishPaths journal{"/job", "/job.tmp", "/job.bak"};
constexpr DeliveryTicket owner{7, 1};
constexpr storage::ScopeView scope{"server-token", "account-token"};
struct SeriesQueueTest : testing::Test {
  uint64_t free = DownloadJobs::Headroom + 10000;
  unsigned links = 0;
  bool linkOk = true;
  storage::SourceIdentity linked{};
  Admission admission{{nullptr, [](void*) { return MemorySnapshot{128 * 1024, 64 * 1024, 0}; },
                       [](void*) { return true; }, [](void*) { return true; }, [](void*) { return true; }},
                      true};
  DownloadPorts ports{this, [](void* p) { return static_cast<SeriesQueueTest*>(p)->free; },
                      [](void* p, const DownloadSpec& selected) {
                        auto& f = *static_cast<SeriesQueueTest*>(p);
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
  bool online = true, writable = true, current = true;
  storage::SeriesQueueStore store{{"/queues", "/queues.tmp", "/queues.bak"}, "/series", scope};
  SeriesQueuePorts queuePorts{
      this, [](void* p) { return static_cast<SeriesQueueTest*>(p)->writable; },
      [](void* p) { return static_cast<SeriesQueueTest*>(p)->online; },
      [](void* p, const storage::SeriesQueueRecord&) { return static_cast<SeriesQueueTest*>(p)->current; }};
  storage::SeriesQueueRecord reviewed{};
  SeriesQueueView view{};
  void review(unsigned count = 3, const char* series = "series") {
    reviewed.clear();
    reviewed.token = 42;
    reviewed.count = count;
    reviewed.request.count = count;
    std::strcpy(reviewed.request.key.data(), series);
    for (unsigned i = 0; i < count; ++i) {
      auto& row = reviewed.books[i];
      row.file = spec;
      row.key.ordinal = i;
      row.file.identity.edition.bytes[0] = i + 1;
      std::snprintf(row.file.file.data(), row.file.file.size(), "file-%u", i);
      std::snprintf(row.file.path.data(), row.file.path.size(), "/books/book-%u.epub", i);
      std::snprintf(row.title.data(), row.title.size(), "Title %u", i);
    }
  }
  void run(SeriesQueue& q, unsigned turns = 100) {
    for (unsigned i = 0; i < turns; ++i) q.tick(owner);
  }

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
    review();
  }
  void complete(DownloadJobs& j) {
    for (unsigned i = 0; i < 20 && j.busy(); ++i) j.tick(owner);
  }
};

TEST_F(SeriesQueueTest, ExactFrozenChoicesRunSequentiallyWithoutOpenIntent) {
  DownloadJobs engine(journal, admission, ports);
  SeriesQueue q(store, engine, source, queuePorts, scope, owner, "/series-job");
  ASSERT_TRUE(q.recover());
  auto id = q.start(reviewed);
  ASSERT_NE(id, 0u);
  EXPECT_EQ(source.opens, 0u);
  run(q);
  ASSERT_TRUE(q.read(id, view));
  EXPECT_TRUE(view.complete);
  EXPECT_EQ(source.opens, 3u);
  EXPECT_EQ(links, 3u);
  for (unsigned i = 0; i < 3; ++i) {
    EXPECT_EQ(view.items[i].state, SeriesItemState::Complete);
    EXPECT_EQ(card.files[reviewed.books[i].file.path.data()], "abc");
  }
  EXPECT_EQ(card.maxHandles, 1);
}
TEST_F(SeriesQueueTest, StalePlanOfflineAndReaderOwnershipDoNotStart) {
  DownloadJobs engine(journal, admission, ports);
  SeriesQueue q(store, engine, source, queuePorts, scope, owner, "/series-job");
  ASSERT_TRUE(q.recover());
  current = false;
  EXPECT_EQ(q.start(reviewed), 0u);
  current = true;
  online = false;
  EXPECT_EQ(q.start(reviewed), 0u);
  online = true;
  writable = false;
  EXPECT_EQ(q.start(reviewed), 0u);
  EXPECT_EQ(source.opens, 0u);
  EXPECT_TRUE(card.files.empty());
}
TEST_F(SeriesQueueTest, RestartIsReadOnlyAndRequiresExplicitResume) {
  uint64_t id;
  {
    DownloadJobs engine(journal, admission, ports);
    SeriesQueue q(store, engine, source, queuePorts, scope, owner, "/series-job");
    ASSERT_TRUE(q.recover());
    id = q.start(reviewed);
    q.tick(owner);
    q.tick(owner);
  }
  card.restart();
  const auto opens = source.opens;
  DownloadJobs engine(journal, admission, ports);
  SeriesQueue q(store, engine, source, queuePorts, scope, owner, "/series-job");
  ASSERT_TRUE(q.recover());
  run(q);
  EXPECT_EQ(source.opens, opens);
  ASSERT_TRUE(q.read(id, view));
  EXPECT_TRUE(view.paused);
  ASSERT_TRUE(q.resume(id));
  run(q);
  ASSERT_TRUE(q.read(id, view));
  EXPECT_TRUE(view.complete);
  EXPECT_EQ(links, 3u);
}
TEST_F(SeriesQueueTest, CompletedRowsAreNotDownloadedAgainAfterRestart) {
  uint64_t id;
  {
    DownloadJobs engine(journal, admission, ports);
    SeriesQueue q(store, engine, source, queuePorts, scope, owner, "/series-job");
    ASSERT_TRUE(q.recover());
    id = q.start(reviewed);
    for (unsigned n = 0; n < 30; ++n) {
      q.tick(owner);
      ASSERT_TRUE(q.read(id, view));
      if (view.items[0].state == SeriesItemState::Complete) break;
    }
    ASSERT_EQ(view.items[0].state, SeriesItemState::Complete);
  }
  card.restart();
  const auto opens = source.opens;
  DownloadJobs engine(journal, admission, ports);
  SeriesQueue q(store, engine, source, queuePorts, scope, owner, "/series-job");
  ASSERT_TRUE(q.recover());
  ASSERT_TRUE(q.resume(id));
  run(q);
  EXPECT_EQ(source.opens - opens, 2u);
  ASSERT_TRUE(q.read(id, view));
  EXPECT_TRUE(view.complete);
}
TEST_F(SeriesQueueTest, CancelRowAndRemainingPreservesCompletedBytes) {
  DownloadJobs engine(journal, admission, ports);
  SeriesQueue q(store, engine, source, queuePorts, scope, owner, "/series-job");
  ASSERT_TRUE(q.recover());
  auto id = q.start(reviewed);
  ASSERT_TRUE(q.cancel(id, 1));
  run(q);
  ASSERT_TRUE(q.read(id, view));
  EXPECT_TRUE(view.complete);
  EXPECT_EQ(view.items[1].state, SeriesItemState::Canceled);
  EXPECT_EQ(source.opens, 2u);
  EXPECT_TRUE(q.cancel(id));
  EXPECT_EQ(card.files["/books/book-0.epub"], "abc");
}
TEST_F(SeriesQueueTest, CancelRunningDrainsBeforeAnyNextFile) {
  DownloadJobs engine(journal, admission, ports);
  SeriesQueue q(store, engine, source, queuePorts, scope, owner, "/series-job");
  ASSERT_TRUE(q.recover());
  auto id = q.start(reviewed);
  q.tick(owner);
  ASSERT_TRUE(engine.busy());
  ASSERT_TRUE(q.cancel(id));
  run(q);
  ASSERT_TRUE(q.read(id, view));
  EXPECT_TRUE(view.complete);
  EXPECT_EQ(source.opens, 1u);
  for (unsigned i = 0; i < 3; ++i) EXPECT_EQ(view.items[i].state, SeriesItemState::Canceled);
}
TEST_F(SeriesQueueTest, DuplicateUsesExistingQueueAndFrozenDestination) {
  DownloadJobs engine(journal, admission, ports);
  SeriesQueue q(store, engine, source, queuePorts, scope, owner, "/series-job");
  ASSERT_TRUE(q.recover());
  auto id = q.start(reviewed);
  std::strcpy(reviewed.books[0].file.path.data(), "/changed.epub");
  EXPECT_EQ(q.start(reviewed), id);
  run(q);
  EXPECT_FALSE(Storage.exists("/changed.epub"));
  EXPECT_EQ(source.opens, 3u);
}
TEST_F(SeriesQueueTest, FourNonterminalQueuesCannotBeEvicted) {
  DownloadJobs engine(journal, admission, ports);
  SeriesQueue q(store, engine, source, queuePorts, scope, owner, "/series-job");
  ASSERT_TRUE(q.recover());
  for (unsigned i = 0; i < 4; ++i) {
    char series[16];
    std::snprintf(series, sizeof(series), "series-%u", i);
    review(1, series);
    ASSERT_NE(q.start(reviewed), 0u);
  }
  review(1, "fifth");
  EXPECT_EQ(q.start(reviewed), 0u);
  EXPECT_EQ(source.opens, 0u);
}
TEST_F(SeriesQueueTest, OldestTerminalQueueCanBeReused) {
  DownloadJobs engine(journal, admission, ports);
  SeriesQueue q(store, engine, source, queuePorts, scope, owner, "/series-job");
  ASSERT_TRUE(q.recover());
  for (unsigned i = 0; i < 5; ++i) {
    char series[16];
    std::snprintf(series, sizeof(series), "series-%u", i);
    review(1, series);
    ASSERT_NE(q.start(reviewed), 0u);
    run(q);
  }
  EXPECT_EQ(q.find("series-0"), 0u);
  EXPECT_NE(q.find("series-4"), 0u);
}
TEST_F(SeriesQueueTest, UnsupportedRowsFailExplicitlyAndDoNotOpenTransport) {
  reviewed.books[1].state = SeriesItemState::Unsupported;
  DownloadJobs engine(journal, admission, ports);
  SeriesQueue q(store, engine, source, queuePorts, scope, owner, "/series-job");
  ASSERT_TRUE(q.recover());
  auto id = q.start(reviewed);
  run(q);
  ASSERT_TRUE(q.read(id, view));
  EXPECT_TRUE(view.complete);
  EXPECT_EQ(view.items[1].state, SeriesItemState::Unsupported);
  EXPECT_EQ(source.opens, 2u);
}
TEST_F(SeriesQueueTest, OrdinaryFileFailureContinuesOtherRowsAndStopsRetryLoop) {
  source.badDigest = true;
  DownloadJobs engine(journal, admission, ports);
  SeriesQueue q(store, engine, source, queuePorts, scope, owner, "/series-job");
  ASSERT_TRUE(q.recover());
  auto id = q.start(reviewed);
  run(q);
  EXPECT_EQ(source.opens, 3u);
  ASSERT_TRUE(q.read(id, view));
  EXPECT_TRUE(view.paused);
  EXPECT_FALSE(view.complete);
  for (unsigned i = 0; i < 3; ++i) EXPECT_EQ(view.items[i].state, SeriesItemState::Failed);
  source.badDigest = false;
  ASSERT_TRUE(q.resume(id));
  run(q);
  ASSERT_TRUE(q.read(id, view));
  EXPECT_TRUE(view.complete);
}
TEST_F(SeriesQueueTest, OwnerChangeRetiresQueueWithoutApplyingLateResult) {
  DownloadJobs engine(journal, admission, ports);
  SeriesQueue q(store, engine, source, queuePorts, scope, owner, "/series-job");
  ASSERT_TRUE(q.recover());
  auto id = q.start(reviewed);
  q.tick(owner);
  q.tick({7, 2});
  run(q);
  EXPECT_FALSE(q.ready());
  EXPECT_FALSE(q.recover());
  EXPECT_FALSE(q.read(id, view));
  EXPECT_EQ(links, 0u);
}
TEST_F(SeriesQueueTest, OfflineBetweenFilesPausesRemainingUntilResume) {
  DownloadJobs engine(journal, admission, ports);
  SeriesQueue q(store, engine, source, queuePorts, scope, owner, "/series-job");
  ASSERT_TRUE(q.recover());
  auto id = q.start(reviewed);
  for (unsigned n = 0; n < 30; ++n) {
    q.tick(owner);
    q.read(id, view);
    if (view.items[0].state == SeriesItemState::Complete) break;
  }
  online = false;
  run(q);
  ASSERT_TRUE(q.read(id, view));
  EXPECT_TRUE(view.paused);
  EXPECT_EQ(source.opens, 1u);
  online = true;
  run(q);
  EXPECT_EQ(source.opens, 1u);
  ASSERT_TRUE(q.resume(id));
  run(q);
  ASSERT_TRUE(q.read(id, view));
  EXPECT_TRUE(view.complete);
}
TEST_F(SeriesQueueTest, CanceledIntentForOtherQueueWaitsForWriterYield) {
  DownloadJobs engine(journal, admission, ports);
  SeriesQueue q(store, engine, source, queuePorts, scope, owner, "/series-job");
  ASSERT_TRUE(q.recover());
  auto first = q.start(reviewed);
  review(2, "other");
  auto second = q.start(reviewed);
  q.tick(owner);
  ASSERT_TRUE(q.cancel(second));
  EXPECT_FALSE(q.read(second, view));
  run(q);
  EXPECT_EQ(source.opens, 3u);
  ASSERT_TRUE(q.read(first, view));
  EXPECT_TRUE(view.complete);
}
TEST_F(SeriesQueueTest, QueueProjectionDoesNotDisturbRunningSelection) {
  DownloadJobs engine(journal, admission, ports);
  SeriesQueue q(store, engine, source, queuePorts, scope, owner, "/series-job");
  ASSERT_TRUE(q.recover());
  auto first = q.start(reviewed);
  review(1, "other");
  auto second = q.start(reviewed);
  q.tick(owner);
  for (unsigned n = 0; n < 100; ++n) {
    q.read(second, view);
    q.tick(owner);
    q.read(first, view);
  }
  ASSERT_TRUE(q.read(first, view));
  EXPECT_TRUE(view.complete);
  ASSERT_TRUE(q.read(second, view));
  EXPECT_TRUE(view.complete);
}
TEST_F(SeriesQueueTest, SequenceNeverRegressesAcrossFileTransitions) {
  DownloadJobs engine(journal, admission, ports);
  SeriesQueue q(store, engine, source, queuePorts, scope, owner, "/series-job");
  ASSERT_TRUE(q.recover());
  auto id = q.start(reviewed);
  uint64_t previous = 0;
  for (unsigned n = 0; n < 100; ++n) {
    q.tick(owner);
    ASSERT_TRUE(q.read(id, view));
    EXPECT_GE(view.sequence, previous);
    previous = view.sequence;
  }
}
TEST_F(SeriesQueueTest, FutureRowsAreCancelableBeforeAnyTransferJobExists) {
  DownloadJobs engine(journal, admission, ports);
  SeriesQueue q(store, engine, source, queuePorts, scope, owner, "/series-job");
  ASSERT_TRUE(q.recover());
  const auto id = q.start(reviewed);
  ASSERT_TRUE(q.read(id, view));
  ASSERT_EQ(view.items[1].job, 0u);
  ASSERT_TRUE(view.items[1].cancelable);
  ASSERT_TRUE(q.cancel(id, 1));
  run(q);
  ASSERT_TRUE(q.read(id, view));
  EXPECT_EQ(view.items[1].state, SeriesItemState::Canceled);
  EXPECT_FALSE(view.items[1].cancelable);
  EXPECT_EQ(source.opens, 2u);
  EXPECT_FALSE(card.files.count(reviewed.books[1].file.path.data()));
}
TEST_F(SeriesQueueTest, JournalSwitchNeverReusesALiveEngineHandle) {
  DownloadJobs engine(journal, admission, ports);
  ASSERT_TRUE(engine.recover(scope, owner));
  const auto old = engine.start(spec, source, owner);
  ASSERT_NE(old, 0u);
  complete(engine);
  ASSERT_EQ(engine.status(owner).state, DownloadState::Complete);
  constexpr storage::PublishPaths other{"/other", "/other.tmp", "/other.bak"};
  ASSERT_TRUE(engine.selectJournal(other, scope, owner));
  auto next = spec;
  std::strcpy(next.path.data(), "/books/next.epub");
  next.identity.edition.bytes[0]++;
  const auto current = engine.start(next, source, owner);
  ASSERT_GT(current, old);
  engine.cancel(old, owner);
  EXPECT_TRUE(engine.busy());
  complete(engine);
  EXPECT_EQ(engine.status(owner).state, DownloadState::Complete);
  ASSERT_TRUE(engine.selectJournal(journal, scope, owner));
  const auto recovered = engine.status(owner).id;
  ASSERT_GT(recovered, current);
  engine.cancel(old, owner);
  EXPECT_NE(engine.status(owner).state, DownloadState::Canceled);
}
}  // namespace
