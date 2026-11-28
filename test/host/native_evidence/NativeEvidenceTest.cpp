#include <HalStorage.h>
#include <gtest/gtest.h>

#include <cstring>
#include <optional>

#include "booksync/server/bookorbit/NativeEvidence.h"
using namespace booksync;
using namespace booksync::server;
using namespace booksync::storage;
namespace {
constexpr ScopeView scope{"server-a", "account-a"};
constexpr SourceIdentity source{{{1}}, {{2}}, {{3}}};
constexpr std::string_view hash = "0123456789abcdef0123456789abcdef";
class Login : public AuthAdapter {
 public:
  AuthCapabilities authCapabilities() const override { return {false, true, false, false}; }
  AuthResult restoreLogin(const CredentialCapture&) override { return {AuthError::None, 42}; }
  AuthError removeLogin(LoginKind) override { return AuthError::None; }
};
class NativeEvidenceFixture : public testing::Test {
 protected:
  Login login;
  Account account{login};
  PageJournal pages{"/pages"};
  LifecycleJournal journal{{"/native", "/native.tmp", "/native.bak"}};
  std::optional<NativeEvidence> evidence;
  std::array<char, 4096> scratch{};
  std::array<char, 1064> proofBytes{};
  std::string_view proof;
  JournalEntry entry;
  void SetUp() override {
    card = {};
    ASSERT_EQ(journal.recover(), JournalResult::Ok);
    ASSERT_EQ(pages.recover(), JournalResult::Ok);
    CredentialCapture capture;
    char key[] = "synthetic-key";
    ASSERT_TRUE(capture.capture(LoginKind::Sync, "synthetic-origin", "synthetic-user", key, sizeof(key) - 1));
    ASSERT_EQ(account.restoreLogin(capture, account.snapshot().ticket), AuthError::None);
    evidence.emplace(account, scope, pages);
    ASSERT_TRUE(NativeEvidence::proof(hash, "Synthetic title", "Synthetic author", proofBytes.data(), proofBytes.size(),
                                      proof));
  }
  void capture(bool closed = true) {
    reader::Capture native(journal);
    ASSERT_EQ(native.open("/book.epub", 1, {source, scope, proof}), JournalResult::Ok);
    reader::Sample sample;
    sample.observedAt = 200;
    ASSERT_EQ(native.update(sample, closed), JournalResult::Ok);
    ASSERT_EQ(journal.read(native.id(), entry, scratch.data(), scratch.size()), JournalResult::Ok);
    // Promotion changes only the outer delivery routing. Native ID, account,
    // source revision, opaque proof and frozen checkpoint bytes remain intact.
    entry.adapter = scope.server;
    entry.owner = scope.account;
  }
};
}  // namespace
TEST_F(NativeEvidenceFixture, MatchUsesCapturedProofAndExactIndependentSource) {
  capture();
  MatchInput input;
  ASSERT_TRUE(evidence->load(entry, input));
  EXPECT_EQ(input.source, source);
  EXPECT_EQ(input.hash, hash);
  EXPECT_EQ(input.path, "/book.epub");
  EXPECT_EQ(input.syncOwner, 42);
  EXPECT_EQ(resolvePath(scope, input.path, source.revision, input.rows, input.count).source, source);
}
TEST_F(NativeEvidenceFixture, LegacyOrOtherAccountCannotAcquireCurrentAccountProof) {
  capture();
  MatchInput input;
  entry.owner = "account-b";
  EXPECT_FALSE(evidence->load(entry, input));
  entry.owner = scope.account;
  entry.payload = "BSO1\1\0\5\0/book";
  EXPECT_FALSE(evidence->load(entry, input));
}
TEST_F(NativeEvidenceFixture, LiveMutableSampleIsNotDeliveryEvidence) {
  capture(false);
  MatchInput input;
  EXPECT_FALSE(evidence->load(entry, input));
}
TEST_F(NativeEvidenceFixture, StatsKeepNativeOpenAcrossDeliveryJournalRenumbering) {
  capture();
  const auto nativeOpen = entry.id;
  std::string checkpoint(119, '\0');
  memcpy(checkpoint.data(), "BSM1", 4);
  memcpy(checkpoint.data() + 4, source.book.bytes.data(), 16);
  memcpy(checkpoint.data() + 20, source.edition.bytes.data(), 16);
  memcpy(checkpoint.data() + 36, source.revision.bytes.data(), 32);
  checkpoint[68] = 1;
  checkpoint[76] = 2;
  checkpoint[84] = 1;
  memcpy(checkpoint.data() + 85, hash.data(), 32);
  checkpoint[117] = char(entry.checkpoint.size());
  checkpoint[118] = char(entry.checkpoint.size() >> 8);
  checkpoint.append(entry.checkpoint);
  entry.checkpoint = checkpoint;
  entry.id = 900;
  StatsInput input;
  ASSERT_TRUE(evidence->load(entry, input));
  EXPECT_EQ(input.open, nativeOpen);
  EXPECT_EQ(input.capturedAt, 200);
  EXPECT_EQ(input.end, 0);
  EXPECT_EQ(input.source, source);
}
TEST_F(NativeEvidenceFixture, OversizedAndUnknownProofAreNotTruncatedSuccess) {
  std::string tooLong(513, 'x');
  EXPECT_FALSE(NativeEvidence::proof(hash, tooLong, "", proofBytes.data(), proofBytes.size(), proof));
  capture();
  std::string unknown(entry.payload);
  auto found = unknown.find("BOE1");
  ASSERT_NE(found, std::string::npos);
  unknown[found + 3] = '2';
  entry.payload = unknown;
  MatchInput input;
  EXPECT_FALSE(evidence->load(entry, input));
}
TEST_F(NativeEvidenceFixture, ConflictingEditionWindowCannotOverrideCapturedProof) {
  capture();
  BindingView rows[2]{{scope, {}, "/book.epub", source}, {scope, {}, "/book.epub", source}};
  rows[1].source.edition.bytes[0] = 9;
  ASSERT_TRUE(evidence->bindings(rows, 2));
  MatchInput input;
  EXPECT_FALSE(evidence->load(entry, input));
  rows[1].scope.account = "account-b";
  EXPECT_TRUE(evidence->load(entry, input));
  EXPECT_EQ(input.source, source);
  EXPECT_FALSE(evidence->bindings(rows, LifecycleMatch::MaxRows + 1));
}
TEST_F(NativeEvidenceFixture, AccountEpochChangeRejectsProviderEvenWhenProcessOwnerIsReused) {
  capture();
  MatchInput input;
  ASSERT_TRUE(evidence->load(entry, input));
  ASSERT_EQ(account.removeLogin(LoginKind::Sync, account.snapshot().ticket), AuthError::None);
  CredentialCapture another;
  char key[] = "another-synthetic-key";
  ASSERT_TRUE(another.capture(LoginKind::Sync, "other-origin", "other-user", key, sizeof(key) - 1));
  ASSERT_EQ(account.restoreLogin(another, account.snapshot().ticket), AuthError::None);
  EXPECT_FALSE(evidence->load(entry, input));
}
