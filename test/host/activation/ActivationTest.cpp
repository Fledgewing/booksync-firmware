#include <gtest/gtest.h>

#include <fstream>
#include <iterator>

#include "booksync/import/Activation.h"
using namespace booksync;
using namespace booksync::legacy;
namespace {
constexpr uint8_t key[6]{2, 0, 0, 0, 0, 1};
std::string fixture(const char* name) {
  std::ifstream in(std::string(FIXTURE_ROOT) + "/sd/.crosspoint/" + name);
  return {std::istreambuf_iterator<char>(in), {}};
}
struct Adapter : AuthAdapter {
  unsigned restores = 0, removes = 0;
  AuthCapabilities authCapabilities() const override { return {false, true, true, true}; }
  AuthResult restoreLogin(const CredentialCapture& capture, LoginAuthorization) override {
    ++restores;
    return capture.ready() && capture.origin() == "synthetic-origin" ? AuthResult{AuthError::None, restores}
                                                                     : AuthResult{};
  }
  AuthError removeLogin(LoginKind) override {
    ++removes;
    return AuthError::None;
  }
};
import::IdentityAudit identity() {
  import::IdentityAudit audit;
  audit.status = import::IdentityStatus::RetainedInPlace;
  audit.identityBlobs = 6;
  audit.joined = true;
  audit.fingerprint.fill(7);
  return audit;
}
void seed() {
  card = {};
  auto sync = fixture("bookorbit.json");
  sync.replace(sync.find("\"serverUrl\": \"\""), 15, "\"serverUrl\": \"synthetic-origin\"");
  card.files["/.crosspoint/bookorbit.json"] = sync;
  card.files["/.crosspoint/wifi.json"] = fixture("wifi.json");
  card.files["/Books/native.epub"] = "native book";
  card.files["/.crosspoint/native-position"] = "native anchor";
  card.files["/.crosspoint/pending-queue"] = "unknown queued work";
  card.disk = card.files;
}
void sourcesUnchanged(const std::map<std::string, std::string>& before) {
  for (const auto& [path, bytes] : before) EXPECT_EQ(card.files.at(path), bytes) << path;
}
}  // namespace
TEST(Activation, RepeatedBootsNeverReimportAndRestoreIndependentSlots) {
  seed();
  auto before = card.files;
  Credentials credentials(key);
  for (unsigned boot = 0; boot < 3; ++boot) {
    Activation activation;
    ASSERT_EQ(activation.boot(credentials, identity()), ActivationResult::Active);
    EXPECT_TRUE(activation.noticePending());
    EXPECT_TRUE(activation.identityReady());
    Adapter adapter;
    Account account(adapter);
    EXPECT_EQ(activation.restore(credentials, CredentialFamily::LibrarySync, account), ImportResult::Restored);
    EXPECT_TRUE(account.snapshot().syncReady);
    EXPECT_FALSE(account.snapshot().signedIn);
    sourcesUnchanged(before);
    card.restart();
  }
}
TEST(Activation, RemovalCannotResurrectEvenWithDamagedMarkerOrChangedLegacy) {
  seed();
  Credentials credentials(key);
  Activation activation;
  ASSERT_EQ(activation.boot(credentials, identity()), ActivationResult::Active);
  Adapter adapter;
  Account account(adapter);
  ASSERT_EQ(activation.restore(credentials, CredentialFamily::LibrarySync, account), ImportResult::Restored);
  ASSERT_EQ(account.removeLogin(LoginKind::Sync, account.snapshot().ticket), AuthError::None);
  auto removed = card.files;
  card.files["/.crosspoint/bookorbit.json"] = fixture("bookorbit.json");
  card.disk = card.files;
  card.restart();
  Activation restart;
  ASSERT_EQ(restart.boot(credentials, identity()), ActivationResult::Active);
  Account fresh(adapter);
  EXPECT_NE(restart.restore(credentials, CredentialFamily::LibrarySync, fresh), ImportResult::Restored);
  EXPECT_FALSE(fresh.snapshot().syncReady);
  EXPECT_EQ(adapter.restores, 1u);
  for (auto& [path, bytes] : card.files)
    if (path.find("removed-") != std::string::npos) bytes = "torn";
  Activation corrupt;
  ASSERT_EQ(corrupt.boot(credentials, identity()), ActivationResult::Active);
  EXPECT_NE(corrupt.restore(credentials, CredentialFamily::LibrarySync, fresh), ImportResult::Restored);
}
TEST(Activation, NoticeDismissalIsDurableAndNeverFallsBackToPending) {
  seed();
  Credentials credentials(key);
  Activation activation;
  ASSERT_EQ(activation.boot(credentials, identity()), ActivationResult::Active);
  ASSERT_TRUE(activation.dismissNotice());
  card.restart();
  Activation restart;
  ASSERT_EQ(restart.boot(credentials, identity()), ActivationResult::Active);
  EXPECT_FALSE(restart.noticePending());
  for (auto& [path, bytes] : card.files)
    if (path.find("notice-done") != std::string::npos) bytes = "damaged";
  EXPECT_FALSE(restart.noticePending());
}
TEST(Activation, ChangedOrUnreadableIdentityNeverAuthorizesTransport) {
  seed();
  Credentials credentials(key);
  Activation activation;
  ASSERT_EQ(activation.boot(credentials, identity()), ActivationResult::Active);
  auto changed = identity();
  changed.fingerprint[0] ^= 1;
  Activation restart;
  EXPECT_EQ(restart.boot(credentials, changed), ActivationResult::Active);
  EXPECT_FALSE(restart.identityReady());
  changed.status = import::IdentityStatus::ReadError;
  EXPECT_EQ(restart.boot(credentials, changed), ActivationResult::Active);
  EXPECT_FALSE(restart.identityReady());
}
TEST(Activation, CutEveryBackupProofActivationBoundaryAndRecover) {
  seed();
  const auto before = card.files;
  Credentials credentials(key);
  Activation complete;
  ASSERT_EQ(complete.boot(credentials, identity()), ActivationResult::Active);
  const auto events = card.events.size();
  ASSERT_GT(events, 30u);
  for (bool eager : {false, true}) {
    for (size_t cut = 0; cut < events; ++cut) {
      seed();
      card.eager = eager;
      card.stop = cut;
      try {
        Activation interrupted;
        interrupted.boot(credentials, identity());
      } catch (const PowerCut&) {
      }
      card.restart();
      Activation restart;
      ASSERT_EQ(restart.boot(credentials, identity()), ActivationResult::Active) << cut;
      sourcesUnchanged(before);
      EXPECT_TRUE(restart.noticePending());
      EXPECT_TRUE(restart.identityReady());
    }
  }
}
TEST(Activation, CutEveryRemovalAndNoticeBoundary) {
  for (bool notice : {false, true}) {
    seed();
    Credentials credentials(key);
    Activation activation;
    ASSERT_EQ(activation.boot(credentials, identity()), ActivationResult::Active);
    const auto initial = card.disk;
    card.events.clear();
    ASSERT_TRUE(notice ? activation.dismissNotice() : activation.remove(CredentialFamily::LibrarySync));
    const auto count = card.events.size();
    for (bool eager : {false, true}) {
      for (size_t cut = 0; cut < count; ++cut) {
        card = {};
        card.files = card.disk = initial;
        card.eager = eager;
        Activation attempt;
        ASSERT_EQ(attempt.boot(credentials, identity()), ActivationResult::Active);
        card.events.clear();
        card.stop = cut;
        try {
          if (notice)
            attempt.dismissNotice();
          else
            attempt.remove(CredentialFamily::LibrarySync);
        } catch (const PowerCut&) {
        }
        card.restart();
        Activation restart;
        ASSERT_EQ(restart.boot(credentials, identity()), ActivationResult::Active);
        // Retrying an unacknowledged command must converge without restoring
        // credentials or showing a notice during the retry.
        ASSERT_TRUE(notice ? restart.dismissNotice() : restart.remove(CredentialFamily::LibrarySync));
        card.restart();
        Activation final;
        ASSERT_EQ(final.boot(credentials, identity()), ActivationResult::Active);
        if (notice)
          EXPECT_FALSE(final.noticePending());
        else {
          Adapter adapter;
          Account account(adapter);
          EXPECT_NE(final.restore(credentials, CredentialFamily::LibrarySync, account), ImportResult::Restored);
        }
      }
    }
  }
}
TEST(Activation, FullReadOnlyUnknownAndCorruptStoresStayOfflineWithoutFallback) {
  for (bool full : {false, true}) {
    seed();
    card.full = full;
    card.readOnly = !full;
    Credentials credentials(key);
    Activation activation;
    EXPECT_EQ(activation.boot(credentials, identity()), ActivationResult::Unavailable);
    EXPECT_FALSE(activation.identityReady());
  }
  seed();
  card.files["/.booksync/import/activation"] = "future";
  Credentials credentials(key);
  Activation activation;
  EXPECT_EQ(activation.boot(credentials, identity()), ActivationResult::Unavailable);
  EXPECT_EQ(card.files.at("/.booksync/import/activation"), "future");
}
TEST(Activation, EveryRecoverableIoFailureRetainsSourcesAndNeverRestoresBeforeCommit) {
  seed();
  const auto before = card.files;
  Credentials credentials(key);
  Activation baseline;
  ASSERT_EQ(baseline.boot(credentials, identity()), ActivationResult::Active);
  const auto count = card.events.size();
  for (size_t failure = 0; failure < count; ++failure) {
    seed();
    card.fail = failure;
    Activation attempt;
    const auto result = attempt.boot(credentials, identity());
    if (result == ActivationResult::Unavailable) {
      Adapter adapter;
      Account account(adapter);
      EXPECT_NE(attempt.restore(credentials, CredentialFamily::LibrarySync, account), ImportResult::Restored);
      EXPECT_FALSE(attempt.identityReady());
    }
    sourcesUnchanged(before);
    card.fail = -1;
    Activation restart;
    EXPECT_EQ(restart.boot(credentials, identity()), ActivationResult::Active) << failure;
    sourcesUnchanged(before);
  }
}
TEST(Activation, ForgottenWifiAndMissingOwnedStoreNeverReplayLegacyOnBoot) {
  seed();
  Credentials credentials(key);
  Activation first;
  ASSERT_EQ(first.boot(credentials, identity()), ActivationResult::Active);
  card.files["/.booksync/wifi.json"] = "{\"credentials\":[],\"lastConnectedSsid\":\"\"}";
  card.disk = card.files;
  card.restart();
  Activation second;
  ASSERT_EQ(second.boot(credentials, identity()), ActivationResult::Active);
  EXPECT_NE(card.files.at("/.booksync/wifi.json"), fixture("wifi.json"));
  card.files.erase("/.booksync/wifi.json");
  card.disk = card.files;
  card.restart();
  Activation missing;
  ASSERT_EQ(missing.boot(credentials, identity()), ActivationResult::Active);
  EXPECT_FALSE(card.files.count("/.booksync/wifi.json"));
  EXPECT_EQ(card.files.at("/.crosspoint/wifi.json"), fixture("wifi.json"));
}
TEST(Activation, FreshInstallDoesNotCreateUpgradeNoticeOrGenerateIdentity) {
  card = {};
  Credentials credentials(key);
  Activation fresh;
  import::IdentityAudit absent;
  absent.status = import::IdentityStatus::Absent;
  ASSERT_EQ(fresh.boot(credentials, absent), ActivationResult::Active);
  EXPECT_FALSE(fresh.noticePending());
  EXPECT_FALSE(fresh.identityReady());
  EXPECT_FALSE(fresh.recoveryRequired());
}
TEST(Activation, TransientPreActivationNvsFailureCanRecoverOnNextBoot) {
  seed();
  const auto before = card.files;
  Credentials credentials(key);
  auto unreadable = identity();
  unreadable.status = import::IdentityStatus::ReadError;
  Activation first;
  EXPECT_EQ(first.boot(credentials, unreadable), ActivationResult::Unavailable);
  EXPECT_FALSE(first.identityReady());
  EXPECT_FALSE(card.files.count("/.booksync/import/activation"));
  card.restart();
  Activation retry;
  EXPECT_EQ(retry.boot(credentials, identity()), ActivationResult::Active);
  EXPECT_TRUE(retry.identityReady());
  sourcesUnchanged(before);
}
TEST(Activation, SignedOutReaderUpgradeHasOnceOnlyNoticeWithoutImportingReaderFiles) {
  card = {};
  card.files["/.crosspoint/native-position"] = "native-anchor";
  card.disk = card.files;
  Credentials credentials(key);
  import::IdentityAudit absent;
  absent.status = import::IdentityStatus::Absent;
  Activation upgrade;
  ASSERT_EQ(upgrade.boot(credentials, absent, true), ActivationResult::Active);
  EXPECT_TRUE(upgrade.noticePending());
  EXPECT_FALSE(upgrade.recoveryRequired());
  ASSERT_TRUE(upgrade.dismissNotice());
  card.restart();
  Activation again;
  ASSERT_EQ(again.boot(credentials, absent, true), ActivationResult::Active);
  EXPECT_FALSE(again.noticePending());
  EXPECT_EQ(card.files.at("/.crosspoint/native-position"), "native-anchor");
}
TEST(Activation, BackupOnlyCredentialsNeverImplicitlyReviveAndAreReportedUnresolved) {
  seed();
  const auto bytes = card.files.at("/.crosspoint/bookorbit.json");
  card.files.erase("/.crosspoint/bookorbit.json");
  card.files["/.crosspoint/bookorbit.json.bak"] = bytes;
  card.disk = card.files;
  Credentials credentials(key);
  Activation upgrade;
  ASSERT_EQ(upgrade.boot(credentials, identity()), ActivationResult::Active);
  EXPECT_TRUE(upgrade.recoveryRequired());
  Adapter adapter;
  Account account(adapter);
  EXPECT_NE(upgrade.restore(credentials, CredentialFamily::LibrarySync, account), ImportResult::Restored);
  EXPECT_EQ(adapter.restores, 0u);
  EXPECT_EQ(card.files.at("/.crosspoint/bookorbit.json.bak"), bytes);
}
TEST(Activation, MissingRetirementMarkerCannotReviveClosedLoginOrNoticeState) {
  seed();
  Credentials credentials(key);
  Activation first;
  ASSERT_EQ(first.boot(credentials, identity()), ActivationResult::Active);
  ASSERT_TRUE(first.remove(CredentialFamily::LibrarySync));
  ASSERT_TRUE(first.dismissNotice());
  for (auto it = card.files.begin(); it != card.files.end();) {
    if (it->first.find("removed-0") != std::string::npos || it->first.find("notice-done") != std::string::npos)
      it = card.files.erase(it);
    else
      ++it;
  }
  card.disk = card.files;
  card.restart();
  Activation again;
  ASSERT_EQ(again.boot(credentials, identity()), ActivationResult::Active);
  Adapter adapter;
  Account account(adapter);
  EXPECT_NE(again.restore(credentials, CredentialFamily::LibrarySync, account), ImportResult::Restored);
  EXPECT_FALSE(again.noticePending());
}
TEST(Activation, ClosedCurrentStateNeverFallsBackToAnOlderLiveBackup) {
  seed();
  Credentials credentials(key);
  Activation first;
  ASSERT_EQ(first.boot(credentials, identity()), ActivationResult::Active);
  ASSERT_TRUE(first.remove(CredentialFamily::LibrarySync));
  ASSERT_TRUE(first.dismissNotice());
  for (auto it = card.files.begin(); it != card.files.end();) {
    if (it->first.find("removed-0") != std::string::npos || it->first.find("notice-done") != std::string::npos)
      it = card.files.erase(it);
    else
      ++it;
  }
  card.files["/.booksync/import/state-0"] = "torn";
  card.files.erase("/.booksync/import/state-10");
  card.disk = card.files;
  card.restart();
  Activation again;
  ASSERT_EQ(again.boot(credentials, identity()), ActivationResult::Active);
  Adapter adapter;
  Account account(adapter);
  EXPECT_NE(again.restore(credentials, CredentialFamily::LibrarySync, account), ImportResult::Restored);
  EXPECT_FALSE(again.noticePending());
  EXPECT_TRUE(again.recoveryRequired());
}
TEST(Activation, CompleteRemovalReceiptFinishesInterruptedCloseWithoutReopeningBackup) {
  seed();
  const auto before = card.files;
  Credentials credentials(key);
  Activation first;
  ASSERT_EQ(first.boot(credentials, identity()), ActivationResult::Active);
  ASSERT_TRUE(first.remove(CredentialFamily::LibrarySync));
  card.files.erase("/.booksync/import/state-0");
  card.disk = card.files;
  card.restart();
  card.readOnly = true;
  Activation readOnly;
  ASSERT_EQ(readOnly.boot(credentials, identity()), ActivationResult::Active);
  EXPECT_TRUE(readOnly.recoveryRequired());
  Adapter adapter;
  Account blocked(adapter);
  EXPECT_NE(readOnly.restore(credentials, CredentialFamily::LibrarySync, blocked), ImportResult::Restored);
  card.readOnly = false;
  Activation recovered;
  ASSERT_EQ(recovered.boot(credentials, identity()), ActivationResult::Active);
  EXPECT_FALSE(recovered.recoveryRequired());
  EXPECT_TRUE(card.files.count("/.booksync/import/state-0"));
  card.files.erase("/.booksync/import/removed-0");
  card.disk = card.files;
  card.restart();
  Activation noMarker;
  ASSERT_EQ(noMarker.boot(credentials, identity()), ActivationResult::Active);
  EXPECT_NE(noMarker.restore(credentials, CredentialFamily::LibrarySync, blocked), ImportResult::Restored);
  sourcesUnchanged(before);
}
