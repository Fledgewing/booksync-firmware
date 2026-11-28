#include <HalStorage.h>
#include <gtest/gtest.h>
#include <openssl/sha.h>

#include <cstring>
#include <optional>

#include "booksync/server/bookorbit/NativeState.h"
using namespace booksync;
using namespace booksync::server;
using namespace booksync::storage;
namespace {
constexpr ScopeView scope{"server-a", "account-a"};
constexpr PublishPaths currentPaths{"/book.state", "/book.state.tmp", "/book.state.bak"};
constexpr PublishPaths capturedPaths{"/book.captured-state", "/book.captured-state.tmp", "/book.captured-state.bak"};
class StateLogin final : public AuthAdapter {
 public:
  AuthCapabilities authCapabilities() const override { return {false, true, false, false}; }
  AuthResult restoreLogin(const CredentialCapture&) override { return {AuthError::None, 42}; }
  AuthError removeLogin(LoginKind) override { return AuthError::None; }
};
class NativeStateTest : public testing::Test {
 protected:
  StateLogin login;
  Account account{login};
  SourceIdentity source{{{1}}, {{2}}, {}};
  bool closed = true;
  std::optional<SourceFile> file;
  std::array<char, NativeStateStore::MaxBytes> currentScratch{}, capturedScratch{};
  NativeStateStore current{currentPaths, currentScratch.data(), currentScratch.size()};
  NativeStateStore captured{capturedPaths, capturedScratch.data(), capturedScratch.size()};
  std::optional<NativeState> evidence;
  LifecycleJournal journal{{"/native", "/native.tmp", "/native.bak"}};
  std::array<char, 4096> journalScratch{};
  JournalEntry entry;
  NativeStateDocument initial;
  StateValues reply{true, "unread", "2026-11-28", true, 0, true, false, {}};
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
    initial.scope = scope;
    initial.source = source;
    initial.token = 5;
    initial.local = {true, "reading", "2026-11-27", true, 4, true, true, "native review"};
    initial.history = {true, "reading", "2026-11-27", true, 4, true, true, "previous remote review"};
    initial.reviewModified = "2026-11-27";
    ASSERT_TRUE(current.save(initial));
    restore();
  }
  void restore() { evidence.emplace(account, scope, *file, captured, current); }
  NativeStateDocument inspect() {
    NativeStateDocument document;
    EXPECT_EQ(current.load(scope, source, document), NativeStateLoad::Ready);
    return document;
  }
};
}  // namespace
TEST_F(NativeStateTest, LoadsFrozenNativeValuesAndPublishesKnownRemoteClearsAfterClose) {
  ASSERT_TRUE(evidence->freeze(entry));
  StateInput input;
  ASSERT_TRUE(evidence->load(entry, input));
  EXPECT_EQ(input.status, "reading");
  EXPECT_EQ(input.rating, 4);
  EXPECT_TRUE(input.reviewPresent);
  EXPECT_EQ(input.reviewNote, "native review");
  EXPECT_EQ(input.capturedToken, input.currentToken);
  const auto native = card.files.at("/native");
  ASSERT_TRUE(evidence->apply(entry, input, reply));
  auto after = inspect();
  EXPECT_EQ(after.local.status, "unread");
  EXPECT_EQ(after.local.rating, 0);
  EXPECT_TRUE(after.local.ratingKnown);
  EXPECT_TRUE(after.local.reviewKnown);
  EXPECT_FALSE(after.local.reviewSet);
  EXPECT_TRUE(after.local.reviewNote.empty());
  EXPECT_EQ(after.token, 5);
  EXPECT_EQ(card.files.at("/native"), native);
  EXPECT_EQ(card.files.at("/book.epub"), "synthetic edition");
}
TEST_F(NativeStateTest, NewManualStatusTokenSurvivesAnOlderReply) {
  ASSERT_TRUE(evidence->freeze(entry));
  auto manual = inspect();
  ++manual.token;
  manual.manualStatusPending = true;
  manual.local.status = "finished";
  ASSERT_TRUE(current.save(manual));
  StateInput input;
  ASSERT_TRUE(evidence->load(entry, input));
  EXPECT_NE(input.capturedToken, input.currentToken);
  EXPECT_EQ(input.status, "reading");
  EXPECT_TRUE(input.manualStatusPending);
  ASSERT_TRUE(evidence->apply(entry, input, reply));
  const auto after = inspect();
  EXPECT_EQ(after.local.status, "finished");
  EXPECT_TRUE(after.manualStatusPending);
  EXPECT_EQ(after.token, 6);
  EXPECT_EQ(after.local.rating, 0);
}
TEST_F(NativeStateTest, UnknownHistoryFlagsDoNotEraseNativeValuesOrKeepStaleCertainty) {
  ASSERT_TRUE(evidence->freeze(entry));
  StateInput input;
  ASSERT_TRUE(evidence->load(entry, input));
  StateValues unknown{false, "future-status", {}, false, 0, false, false, {}};
  ASSERT_TRUE(evidence->apply(entry, input, unknown));
  auto after = inspect();
  EXPECT_EQ(after.local.status, "reading");
  EXPECT_EQ(after.local.rating, 4);
  EXPECT_EQ(after.local.reviewNote, "native review");
  EXPECT_FALSE(after.history.statusKnown);
  EXPECT_FALSE(after.history.ratingKnown);
  EXPECT_FALSE(after.history.reviewKnown);
  EXPECT_EQ(after.history.status, "future-status");
}
TEST_F(NativeStateTest, CaptureCannotBeReplacedWithNewCurrentValues) {
  ASSERT_TRUE(evidence->freeze(entry));
  auto changed = inspect();
  ++changed.token;
  changed.local.rating = 5;
  ASSERT_TRUE(current.save(changed));
  EXPECT_FALSE(evidence->freeze(entry));
  StateInput input;
  ASSERT_TRUE(evidence->load(entry, input));
  EXPECT_EQ(input.rating, 4);
}
TEST_F(NativeStateTest, ClosedRevisionAndAccountEpochAreRecheckedAtApplication) {
  ASSERT_TRUE(evidence->freeze(entry));
  StateInput input;
  ASSERT_TRUE(evidence->load(entry, input));
  const auto before = card.files.at(currentPaths.current);
  closed = false;
  EXPECT_FALSE(evidence->apply(entry, input, reply));
  closed = true;
  card.files["/book.epub"][0] = 'x';
  EXPECT_FALSE(evidence->apply(entry, input, reply));
  card.files["/book.epub"][0] = 's';
  ASSERT_EQ(account.removeLogin(LoginKind::Sync, account.snapshot().ticket), AuthError::None);
  EXPECT_FALSE(evidence->apply(entry, input, reply));
  EXPECT_EQ(card.files.at(currentPaths.current), before);
}
TEST_F(NativeStateTest, AnotherEntryOrScopeCannotApplyTheFrozenResult) {
  ASSERT_TRUE(evidence->freeze(entry));
  StateInput input;
  ASSERT_TRUE(evidence->load(entry, input));
  auto other = entry;
  ++other.id;
  EXPECT_FALSE(evidence->apply(other, input, reply));
  other = entry;
  other.document = "other-document";
  EXPECT_FALSE(evidence->apply(other, input, reply));
  input.scope.account = "account-b";
  EXPECT_FALSE(evidence->apply(entry, input, reply));
}
TEST_F(NativeStateTest, EveryNativePublicationCutReplaysWithoutChangingTheUserToken) {
  ASSERT_TRUE(evidence->freeze(entry));
  StateInput input;
  ASSERT_TRUE(evidence->load(entry, input));
  const auto before = card;
  card.events.clear();
  ASSERT_TRUE(evidence->apply(entry, input, reply));
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
        evidence->apply(entry, input, reply);
      } catch (const PowerCut&) {
      }
      card.restart();
      restore();
      ASSERT_TRUE(evidence->load(entry, input)) << cut;
      ASSERT_TRUE(evidence->apply(entry, input, reply)) << cut;
      const auto after = inspect();
      EXPECT_EQ(after.token, 5) << cut;
      EXPECT_EQ(after.local.status, "unread") << cut;
      EXPECT_EQ(after.local.rating, 0) << cut;
      EXPECT_EQ(card.handles, 0) << cut;
    }
}
TEST_F(NativeStateTest, CapturedManualIntentStillSuppressesStatusAfterItsPendingFlagClears) {
  initial.manualStatusPending = true;
  ASSERT_TRUE(current.save(initial));
  ASSERT_TRUE(evidence->freeze(entry));
  initial.manualStatusPending = false;
  ASSERT_TRUE(current.save(initial));
  StateInput input;
  ASSERT_TRUE(evidence->load(entry, input));
  EXPECT_TRUE(input.manualStatusPending);
  ASSERT_TRUE(evidence->apply(entry, input, reply));
  EXPECT_EQ(inspect().local.status, "reading");
}
TEST_F(NativeStateTest, OverlappingCaptureAndCurrentWorkspacesAreRejected) {
  NativeStateStore aliased(capturedPaths, currentScratch.data(), currentScratch.size());
  NativeState unsafe(account, scope, *file, aliased, current);
  EXPECT_FALSE(unsafe.freeze(entry));
  EXPECT_FALSE(card.files.count(capturedPaths.current));
}
TEST_F(NativeStateTest, MaskedStagedStatusStillRetainsTheOriginalKnownHistory) {
  ASSERT_TRUE(evidence->freeze(entry));
  auto manual = inspect();
  ++manual.token;
  manual.manualStatusPending = true;
  manual.local.status = "finished";
  ASSERT_TRUE(current.save(manual));
  const auto put = [](std::string& bytes, size_t at, uint64_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i) bytes[at + i] = static_cast<char>(value >> (8 * i));
  };
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
  std::string staged(14, '\0');
  staged.replace(0, 4, "BSS1");
  staged[4] = 7;  // Original status/rating/review all known; review explicitly cleared.
  put(staged, 6, matched.size(), 2);
  put(staged, 8, reply.status.size(), 2);
  put(staged, 10, reply.statusModified.size(), 2);
  staged.append(matched).append(reply.status).append(reply.statusModified);
  entry.checkpoint = staged;
  entry.acknowledged = 7;
  entry.attempting = 4;
  StateInput input;
  ASSERT_TRUE(evidence->load(entry, input));
  auto masked = reply;
  masked.statusKnown = false;  // Same masking performed by the F18d handler.
  ASSERT_TRUE(evidence->apply(entry, input, masked));
  const auto after = inspect();
  EXPECT_EQ(after.local.status, "finished");
  EXPECT_TRUE(after.history.statusKnown);
  EXPECT_EQ(after.history.status, "unread");
  EXPECT_TRUE(after.history.ratingKnown);
  EXPECT_EQ(after.history.rating, 0);
}
