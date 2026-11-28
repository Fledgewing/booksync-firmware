// SPDX-License-Identifier: GPL-3.0-or-later
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <array>
#include <cstring>

#include "booksync/server/bookorbit/AnnotationSpool.h"
using namespace booksync;
namespace {
struct Rows : server::AnnotationRecordReader {
  size_t count = 2, fail = SIZE_MAX, zeroReads = 0;
  bool changeSecondPass = false;
  std::string text = std::string(10000, 'x');
  server::AnnotationRecord value;
  const server::AnnotationRecord* at(size_t index) override {
    if (index >= count || index == fail) return nullptr;
    if (index == 0 && ++zeroReads == 2 && changeSecondPass) text[0] = 'y';
    value = {};
    value.change.row.serverId = index + 1;
    value.change.row.text = text;
    value.nativeId[0] = uint8_t(index + 1);
    return &value;
  }
};
struct Keys : server::AnnotationKeyReader {
  server::AnnotationKey value{"0123456789abcdef0123456789abcdef", "2026-01-02 03:04:05"};
  const server::AnnotationKey* at(size_t) override { return &value; }
};
struct NativeAnnotationSpoolTest : testing::Test {
  std::array<char, server::AnnotationRecordCodec::MaxBytes> scratch{};
  std::string payload;
  server::AnnotationSpoolBinding binding;
  Rows rows;
  Keys keys;
  void SetUp() override {
    card = {};
    binding.scope = {"server-token", "account-token"};
    binding.source.book.bytes[0] = 1;
    binding.source.edition.bytes[0] = 2;
    binding.source.revision.bytes[0] = 3;
    binding.entry = 4;
    binding.nativeOpen = 5;
    binding.document = "document-token";
    payload.assign(80, 0);
    std::memcpy(payload.data(), "BSO2", 4);
    auto put = [&](size_t at, size_t n) {
      payload[at] = char(n);
      payload[at + 1] = char(n >> 8);
    };
    put(4, 1);
    put(6, 2);
    put(8, binding.scope.server.size());
    put(10, binding.scope.account.size());
    put(12, 1);
    std::memcpy(payload.data() + 16, binding.source.book.bytes.data(), 16);
    std::memcpy(payload.data() + 32, binding.source.edition.bytes.data(), 16);
    std::memcpy(payload.data() + 48, binding.source.revision.bytes.data(), 32);
    payload += "/b";
    payload += binding.scope.server;
    payload += binding.scope.account;
    payload += "p";
    binding.payload = payload;
  }
  server::AnnotationSpool spool(bool pending = false) {
    return {{"/spool", "/spool.tmp", "/spool.bak"},
            binding,
            server::AnnotationKind::Highlight,
            pending,
            scratch.data(),
            scratch.size()};
  }
};
TEST_F(NativeAnnotationSpoolTest, FrozenCaptureStreamsLargeRowsAndCompleteKeysWithoutRecapture) {
  auto file = spool();
  server::AnnotationSpoolState state;
  state.rows = 2;
  state.keys = 1024;
  state.flags = 63;
  state.nativeGeneration = 9;
  ASSERT_TRUE(file.create(state, rows, &keys));
  auto reopened = spool();
  server::AnnotationSpoolState loaded;
  ASSERT_TRUE(reopened.load(loaded));
  EXPECT_EQ(loaded.keys, 1024U);
  EXPECT_EQ(loaded.nativeGeneration, 9U);
  ASSERT_NE(reopened.at(1), nullptr);
  EXPECT_EQ(reopened.at(0)->change.row.text.size(), 10000U);
  server::AnnotationKey key;
  ASSERT_TRUE(reopened.key(1023, key));
  EXPECT_EQ(key.datetime, keys.value.datetime);
  rows.text = "newer native edit";
  EXPECT_FALSE(reopened.create(state, rows, &keys));
  ASSERT_TRUE(reopened.load(loaded));
  EXPECT_EQ(reopened.at(0)->change.row.text.size(), 10000U);
  EXPECT_EQ(card.maxHandles, 1);
}
TEST_F(NativeAnnotationSpoolTest, KeyStreamingReusesRecordStorageAndClosesEveryReader) {
  auto file = spool();
  server::AnnotationSpoolState state;
  state.rows = 2;
  state.keys = 1024;
  ASSERT_TRUE(file.create(state, rows, &keys));
  ASSERT_TRUE(file.load(state));
  const auto before = std::count(card.events.begin(), card.events.end(), "open");
  server::AnnotationKey key;
  for (size_t i = 0; i < 1024; ++i) {
    ASSERT_TRUE(file.key(i, key));
    EXPECT_EQ(key.key, keys.value.key);
  }
  EXPECT_EQ(std::count(card.events.begin(), card.events.end(), "open") - before, 2);
  ASSERT_NE(file.at(0), nullptr);
  ASSERT_TRUE(file.key(1023, key));
  EXPECT_EQ(key.datetime, keys.value.datetime);
  EXPECT_EQ(card.handles, 0);
  EXPECT_EQ(card.maxHandles, 1);
}
TEST_F(NativeAnnotationSpoolTest, PositiveLoadRequiresTheFinalFilesystemBarrier) {
  auto file = spool();
  server::AnnotationSpoolState state;
  state.rows = 2;
  ASSERT_TRUE(file.create(state, rows));
  card.readOnly = true;
  EXPECT_FALSE(file.load(state));
  EXPECT_EQ(file.at(0), nullptr);
  card.readOnly = false;
  ASSERT_TRUE(file.load(state));
  EXPECT_NE(file.at(0), nullptr);
}
TEST_F(NativeAnnotationSpoolTest, ReplyPersistsAllSixHundredRowsBeforeExposingAny) {
  auto file = spool(true);
  rows.count = 600;
  server::AnnotationSpoolState state;
  state.rows = 600;
  state.round = 7;
  state.nextCursor = 16;
  state.nextDone = true;
  ASSERT_TRUE(file.create(state, rows));
  server::AnnotationSpoolState loaded;
  ASSERT_TRUE(file.load(loaded));
  EXPECT_EQ(loaded.rows, 600U);
  EXPECT_EQ(loaded.round, 7U);
  EXPECT_TRUE(loaded.nextDone);
  for (size_t i = 0; i < 600; ++i) {
    const auto* record = file.at(i);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->change.row.serverId, i + 1);
  }
}
TEST_F(NativeAnnotationSpoolTest, IncompleteReplyNeverPublishesAndDoesNotAdoptStaging) {
  auto file = spool(true);
  rows.count = 600;
  rows.fail = 599;
  server::AnnotationSpoolState state;
  state.rows = 600;
  EXPECT_FALSE(file.create(state, rows));
  server::AnnotationSpoolState loaded;
  EXPECT_FALSE(file.load(loaded));
  EXPECT_FALSE(card.files.contains("/spool"));
}
TEST_F(NativeAnnotationSpoolTest, RejectsChangedFullTupleAndCorruptionWithoutOverwriting) {
  auto file = spool();
  server::AnnotationSpoolState state;
  state.rows = 2;
  ASSERT_TRUE(file.create(state, rows));
  binding.nativeOpen++;
  auto changed = spool();
  server::AnnotationSpoolState loaded;
  EXPECT_FALSE(changed.load(loaded));
  EXPECT_FALSE(changed.create(state, rows));
  binding.nativeOpen--;
  card.files["/spool"].back() ^= 1;
  auto damaged = spool();
  EXPECT_FALSE(damaged.load(loaded));
  EXPECT_FALSE(damaged.create(state, rows));
}
TEST_F(NativeAnnotationSpoolTest, ChangingSecondPassNeverCreatesAuthority) {
  auto file = spool(true);
  rows.changeSecondPass = true;
  server::AnnotationSpoolState state;
  state.rows = 2;
  EXPECT_FALSE(file.create(state, rows));
  server::AnnotationSpoolState loaded;
  EXPECT_FALSE(file.load(loaded));
  EXPECT_FALSE(card.files.contains("/spool"));
}
TEST_F(NativeAnnotationSpoolTest, AdmissionRevocationBlocksCreateLoadAndReplay) {
  bool admitted = true;
  server::AnnotationSpool file(
      {"/spool", "/spool.tmp", "/spool.bak"}, binding, server::AnnotationKind::Highlight, true, scratch.data(),
      scratch.size(), [](void* context) { return *static_cast<bool*>(context); }, &admitted);
  server::AnnotationSpoolState state;
  state.rows = 2;
  admitted = false;
  EXPECT_FALSE(file.create(state, rows));
  EXPECT_FALSE(card.files.contains("/spool"));
  admitted = true;
  ASSERT_TRUE(file.create(state, rows));
  server::AnnotationSpoolState loaded;
  ASSERT_TRUE(file.load(loaded));
  admitted = false;
  EXPECT_EQ(file.at(0), nullptr);
  EXPECT_FALSE(file.load(loaded));
}
TEST_F(NativeAnnotationSpoolTest, EveryPublicationCutHasEitherNoAuthorityOrTheCompleteFrozenReply) {
  auto file = spool(true);
  server::AnnotationSpoolState state;
  state.rows = 2;
  ASSERT_TRUE(file.create(state, rows));
  const auto events = card.events.size();
  for (size_t cut = 0; cut < events; ++cut) {
    card = {};
    card.stop = int(cut);
    card.eager = true;
    try {
      auto interrupted = spool(true);
      interrupted.create(state, rows);
    } catch (const PowerCut&) {
    }
    card.restart();
    auto restarted = spool(true);
    server::AnnotationSpoolState loaded;
    if (restarted.load(loaded)) {
      EXPECT_EQ(loaded.rows, 2U);
      ASSERT_NE(restarted.at(1), nullptr);
      EXPECT_EQ(restarted.at(1)->change.row.text, rows.text);
    }
  }
}
}  // namespace
