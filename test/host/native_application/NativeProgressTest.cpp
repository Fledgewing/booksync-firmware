#include <HalStorage.h>
#include <gtest/gtest.h>
#include <openssl/sha.h>

#include <cstring>
#include <optional>

#include "booksync/server/bookorbit/NativeProgress.h"
using namespace booksync;
using namespace booksync::server;
using namespace booksync::storage;
namespace {
constexpr ScopeView scope{"server-a", "account-a"};
constexpr PublishPaths capturePaths{"/captured", "/captured.tmp", "/captured.bak"};
constexpr PublishPaths positionPaths{"/position", "/position.tmp", "/position.bak"};
void put(std::string& bytes, size_t at, uint64_t value, unsigned count) {
  for (unsigned i = 0; i < count; ++i) bytes[at + i] = static_cast<char>(value >> (8 * i));
}
class Login final : public AuthAdapter {
 public:
  AuthCapabilities authCapabilities() const override { return {false, true, false, false}; }
  AuthResult restoreLogin(const CredentialCapture&) override { return {AuthError::None, 42}; }
  AuthError removeLogin(LoginKind) override { return AuthError::None; }
};
class NativeProgressTest : public testing::Test {
 protected:
  Login login;
  Account account{login};
  SourceIdentity source{{{1}}, {{2}}, {}};
  bool closed = true;
  std::optional<SourceFile> file;
  std::array<char, ProgressCapture::MaxBytes> captureScratch{};
  std::array<char, PositionStore::MaxBytes> projectionScratch{}, inspectScratch{};
  std::array<char, 4096> journalScratch{};
  ProgressCapture capture{capturePaths, captureScratch.data(), captureScratch.size()};
  PositionStore position{positionPaths};
  std::optional<NativeProgress> evidence;
  LifecycleJournal journal{{"/native", "/native.tmp", "/native.bak"}};
  JournalEntry entry;
  PositionValue local{true, true, .25, 200, NativeProgress::ProtocolLocator, 1, "raw/protocol-place", "local"};
  ProgressValues remote{true, true, .5, 250, "raw/future-remote-place", "other reader"};
  void SetUp() override {
    card = {};
    constexpr std::string_view bytes = "synthetic edition";
    card.files["/book.epub"] = bytes;
    card.disk = card.files;
    SHA256(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), source.revision.bytes.data());
    file.emplace(
        BindingView{scope, {}, "/book.epub", source}, [](void* context) { return *static_cast<bool*>(context); },
        &closed);
    CredentialCapture credential;
    char secret[] = "synthetic";
    ASSERT_TRUE(credential.capture(LoginKind::Sync, "synthetic-origin", "synthetic-user", secret, sizeof(secret) - 1));
    ASSERT_EQ(account.restoreLogin(credential, account.snapshot().ticket), AuthError::None);
    ASSERT_EQ(journal.recover(), JournalResult::Ok);
    reader::Capture native(journal);
    ASSERT_EQ(native.open("/book.epub", 1, {source, scope, "opaque"}), JournalResult::Ok);
    reader::Sample sample;
    sample.observedAt = 200;
    ASSERT_EQ(native.update(sample, true), JournalResult::Ok);
    ASSERT_EQ(journal.read(native.id(), entry, journalScratch.data(), journalScratch.size()), JournalResult::Ok);
    entry.adapter = scope.server;
    entry.owner = scope.account;
    entry.id = 900;
    restore();
  }
  void restore() {
    evidence.emplace(account, scope, *file, capture, position, projectionScratch.data(), projectionScratch.size());
  }
  PositionRecord inspect() {
    PositionRecord out;
    EXPECT_EQ(position.load(scope, source, out, inspectScratch.data(), inspectScratch.size()), PositionLoad::Ready);
    return out;
  }
};
}  // namespace
TEST_F(NativeProgressTest, DefaultFetchRetainsUnknownLocalBytesAndPublishesOnlyF19Projection) {
  auto unknown = local;
  unknown.kind = 77;
  unknown.version = 3;
  unknown.raw = std::string_view("native\0unknown", 14);
  ASSERT_TRUE(evidence->freeze(entry, unknown, false));
  const auto nativeBefore = card.files.at("/native");
  ProgressInput input;
  ASSERT_TRUE(evidence->load(entry, input));
  EXPECT_FALSE(input.deliver);
  EXPECT_TRUE(input.position.empty());
  ASSERT_TRUE(evidence->apply(entry, input, remote));
  auto out = inspect();
  EXPECT_EQ(out.remote.raw, remote.position);
  EXPECT_EQ(out.remote.percentage, .5);
  EXPECT_EQ(out.remote.at, remote.timestamp);
  EXPECT_EQ(card.files.at("/native"), nativeBefore);
  EXPECT_EQ(card.files.at("/book.epub"), "synthetic edition");
  FrozenProgress retained;
  ASSERT_TRUE(capture.load(scope, source, entry.id, 1, entry.payload, entry.document, retained));
  EXPECT_EQ(retained.value.raw, unknown.raw);
}
TEST_F(NativeProgressTest, OutgoingCaptureRequiresSavedF19PermissionAndSupportedLosslessLocator) {
  ASSERT_TRUE(evidence->freeze(entry, local, true));
  ProgressInput input;
  EXPECT_FALSE(evidence->load(entry, input));
  PositionRecord policy;
  policy.scope = scope;
  policy.source = source;
  policy.token = 1;
  policy.allowPush = true;
  ASSERT_TRUE(position.save(policy));
  ASSERT_TRUE(evidence->load(entry, input));
  EXPECT_TRUE(input.deliver);
  EXPECT_EQ(input.position, local.raw);
  EXPECT_EQ(input.capturedAt, local.at);
  ASSERT_TRUE(evidence->apply(entry, input, remote));
  auto out = inspect();
  EXPECT_EQ(out.remote, local);
  EXPECT_EQ(out.marker, local);
  EXPECT_FALSE(out.allowPush);
  ASSERT_TRUE(evidence->apply(entry, input, remote));
  EXPECT_EQ(inspect().token, out.token);
}
TEST_F(NativeProgressTest, UnknownLocatorCannotBecomeAnOutgoingIntent) {
  auto unknown = local;
  unknown.kind = 77;
  EXPECT_FALSE(evidence->freeze(entry, unknown, true));
  unknown = local;
  unknown.raw = std::string_view("raw\0binary", 10);
  EXPECT_FALSE(evidence->freeze(entry, unknown, true));
  EXPECT_FALSE(card.files.count(capturePaths.current));
}
TEST_F(NativeProgressTest, AccountTransitionAndChangedSourcePreventPublication) {
  ASSERT_TRUE(evidence->freeze(entry, local, false));
  ProgressInput input;
  ASSERT_TRUE(evidence->load(entry, input));
  card.files["/book.epub"][0] = 'x';
  EXPECT_FALSE(evidence->apply(entry, input, remote));
  card.files["/book.epub"][0] = 's';
  closed = false;
  EXPECT_FALSE(evidence->apply(entry, input, remote));
  closed = true;
  ASSERT_EQ(account.removeLogin(LoginKind::Sync, account.snapshot().ticket), AuthError::None);
  EXPECT_FALSE(evidence->apply(entry, input, remote));
  EXPECT_FALSE(card.files.count(positionPaths.current));
}
TEST_F(NativeProgressTest, CannotFlipFrozenFetchToDeliveryOrApplyAnotherEntry) {
  ASSERT_TRUE(evidence->freeze(entry, local, false));
  EXPECT_FALSE(evidence->freeze(entry, local, true));
  ProgressInput input;
  ASSERT_TRUE(evidence->load(entry, input));
  auto other = entry;
  ++other.id;
  EXPECT_FALSE(evidence->apply(other, input, remote));
  other = entry;
  other.document = "other-document";
  EXPECT_FALSE(evidence->apply(other, input, remote));
  input.deliver = true;
  EXPECT_FALSE(evidence->apply(entry, input, remote));
  EXPECT_FALSE(card.files.count(positionPaths.current));
}
TEST_F(NativeProgressTest, EveryProjectionCutReplaysWithoutMovingNativePlaceOrMintingAnotherToken) {
  ASSERT_TRUE(evidence->freeze(entry, local, false));
  ProgressInput input;
  ASSERT_TRUE(evidence->load(entry, input));
  const auto before = card;
  card.events.clear();
  ASSERT_TRUE(evidence->apply(entry, input, remote));
  const auto steps = card.events.size();
  for (bool eager : {false, true})
    for (size_t cut = 0; cut < steps; ++cut) {
      card = before;
      card.events.clear();
      card.eager = eager;
      restore();
      ASSERT_TRUE(evidence->load(entry, input));
      card.events.clear();
      card.stop = static_cast<int>(cut);
      try {
        evidence->apply(entry, input, remote);
      } catch (const PowerCut&) {
      }
      card.restart();
      restore();
      ASSERT_TRUE(evidence->load(entry, input)) << cut;
      ASSERT_TRUE(evidence->apply(entry, input, remote)) << cut;
      const auto out = inspect();
      EXPECT_EQ(out.token, 1) << cut;
      EXPECT_EQ(out.remote.raw, remote.position) << cut;
      EXPECT_EQ(card.files.at("/native"), before.files.at("/native")) << cut;
      EXPECT_EQ(card.handles, 0) << cut;
    }
}
TEST_F(NativeProgressTest, OverrideCommitPreservesItsWatermarkAcrossPublicationReplay) {
  PositionRecord policy;
  policy.scope = scope;
  policy.source = source;
  policy.token = 1;
  policy.allowPush = true;
  policy.overridePlace = local;
  ASSERT_TRUE(position.save(policy));
  ASSERT_TRUE(evidence->freeze(entry, local, true, true));
  ProgressInput input;
  ASSERT_TRUE(evidence->load(entry, input));
  ASSERT_TRUE(evidence->apply(entry, input, remote));
  const auto committed = inspect();
  EXPECT_EQ(committed.lastOverrideAt, local.at);
  EXPECT_FALSE(committed.overridePlace.present);
  ASSERT_TRUE(evidence->apply(entry, input, remote));
  EXPECT_EQ(inspect().lastOverrideAt, local.at);
  EXPECT_EQ(inspect().token, committed.token);
}
TEST_F(NativeProgressTest, NewOverrideCannotBeErasedByAnOlderOrdinaryCapture) {
  ASSERT_TRUE(evidence->freeze(entry, local, true));
  PositionRecord policy;
  policy.scope = scope;
  policy.source = source;
  policy.token = 1;
  policy.allowPush = true;
  policy.overridePlace = local;
  policy.overridePlace.at = local.at + 1;
  ASSERT_TRUE(position.save(policy));
  ProgressInput input;
  EXPECT_FALSE(evidence->load(entry, input));
  EXPECT_EQ(inspect().overridePlace.at, local.at + 1);
}
TEST_F(NativeProgressTest, BrowserProvenanceAndExactPlaceRedatingSurviveProjection) {
  PositionRecord policy;
  policy.scope = scope;
  policy.source = source;
  policy.token = 1;
  policy.marker = {true, true, remote.percentage, 100, NativeProgress::ProtocolLocator, 1, remote.position, "old"};
  ASSERT_TRUE(position.save(policy));
  ASSERT_TRUE(evidence->freeze(entry, local, false));
  ProgressInput input;
  ASSERT_TRUE(evidence->load(entry, input));
  remote.web = true;
  ASSERT_TRUE(evidence->apply(entry, input, remote));
  EXPECT_TRUE(inspect().remote.browser);
  EXPECT_TRUE(inspect().remote.redated);
}
TEST_F(NativeProgressTest, PublishedMarkerAllowsLocalRecoveryOnlyWithConfirmedPhaseCheckpoint) {
  PositionRecord policy;
  policy.scope = scope;
  policy.source = source;
  policy.token = 1;
  policy.allowPush = true;
  ASSERT_TRUE(position.save(policy));
  ASSERT_TRUE(evidence->freeze(entry, local, true));
  ProgressInput input;
  ASSERT_TRUE(evidence->load(entry, input));
  ASSERT_TRUE(evidence->apply(entry, input, remote));
  const auto token = inspect().token;
  restore();
  // Matching local values are not authorization for another outgoing PUT.
  EXPECT_FALSE(evidence->load(entry, input));
  std::string matched(119, '\0');
  matched.replace(0, 4, "BSM1");
  std::memcpy(matched.data() + 4, source.book.bytes.data(), 16);
  std::memcpy(matched.data() + 20, source.edition.bytes.data(), 16);
  std::memcpy(matched.data() + 36, source.revision.bytes.data(), 32);
  put(matched, 68, 1, 8);
  put(matched, 76, 2, 8);
  matched[84] = 1;
  matched.replace(85, 32, "0123456789abcdef0123456789abcdef");
  put(matched, 117, entry.checkpoint.size(), 2);
  matched.append(entry.checkpoint);
  std::string pending(28, '\0');
  pending.replace(0, 4, "BSP1");
  pending[4] = 3;
  put(pending, 6, matched.size(), 2);
  put(pending, 8, remote.position.size(), 2);
  put(pending, 10, remote.label.size(), 2);
  std::memcpy(pending.data() + 12, &remote.percentage, 8);
  put(pending, 20, remote.timestamp, 8);
  pending.append(matched).append(remote.position).append(remote.label);
  entry.checkpoint = pending;
  restore();
  ASSERT_TRUE(evidence->load(entry, input));
  ASSERT_TRUE(evidence->apply(entry, input, remote));
  EXPECT_EQ(inspect().token, token);
  pending[3] = '9';
  EXPECT_FALSE(evidence->load(entry, input));
}
TEST_F(NativeProgressTest, AReusedProcessOwnerCannotReviveTheOldAccountEpoch) {
  ASSERT_TRUE(evidence->freeze(entry, local, false));
  ASSERT_EQ(account.removeLogin(LoginKind::Sync, account.snapshot().ticket), AuthError::None);
  CredentialCapture credential;
  char secret[] = "synthetic";
  ASSERT_TRUE(credential.capture(LoginKind::Sync, "synthetic-origin", "synthetic-user", secret, sizeof(secret) - 1));
  ASSERT_EQ(account.restoreLogin(credential, account.snapshot().ticket), AuthError::None);
  EXPECT_EQ(account.snapshot().syncOwner, 42);
  ProgressInput input;
  EXPECT_FALSE(evidence->load(entry, input));
}
TEST_F(NativeProgressTest, OverlappingCaptureAndProjectionWorkspacesFailBeforePublication) {
  ProgressCapture aliasedCapture(capturePaths, projectionScratch.data(), ProgressCapture::MaxBytes);
  NativeProgress aliased(account, scope, *file, aliasedCapture, position, projectionScratch.data(),
                         projectionScratch.size());
  ASSERT_TRUE(aliased.freeze(entry, local, false));
  ProgressInput input;
  ASSERT_TRUE(aliased.load(entry, input));
  EXPECT_FALSE(aliased.apply(entry, input, remote));
  EXPECT_FALSE(card.files.count(positionPaths.current));
}
