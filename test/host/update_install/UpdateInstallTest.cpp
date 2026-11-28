#include <HalStorage.h>
#include <gtest/gtest.h>

// The real HAL exposes Storage as a macro; public policy identifiers must
// compile with that macro present, unlike the host fake variable.
#define Storage HalStorage::getInstance()
#include "booksync/system/Update.h"
#undef Storage

#include "../../fixtures/update_install/FakeUpdate.h"

TEST(UpdateInstall, DeferredInstallReturnAndBootRecovery) {
  update_fixture::FakeUpdate fake;
  unsigned checks = 0;
  update_fixture::scenario(fake, [&](bool ok) {
    ++checks;
    EXPECT_TRUE(ok) << checks;
  });
  EXPECT_GE(checks, 20u);
}
TEST(UpdateInstall, RejectsWrongBoardMapTruncationAndTrust) {
  update_fixture::FakeUpdate f;
  f.image.board = booksync::update::Board::Classic;
  EXPECT_EQ(f.install(), booksync::update::Result::Board);
  f.image.board = booksync::update::Board::Pro;
  f.image.layout[0] ^= 1;
  EXPECT_EQ(f.install(), booksync::update::Result::Layout);
  f.image.layout[0] ^= 1;
  f.image.size--;
  EXPECT_EQ(f.install(), booksync::update::Result::Incomplete);
  f.image.size++;
  f.image.digest[0] ^= 1;
  EXPECT_EQ(f.install(), booksync::update::Result::Trust);
  EXPECT_EQ(f.writes, 0u);
}
TEST(UpdateInstall, MissingCompatibilityProofNeverOffersReturn) {
  update_fixture::FakeUpdate f;
  EXPECT_FALSE(f.service.canReturn());
  f.compatible = true;
  EXPECT_TRUE(f.service.canReturn());
  f.previous.complete = false;
  EXPECT_FALSE(f.service.canReturn());
  EXPECT_EQ(f.service.returnPrevious(), booksync::update::Result::Unavailable);
  EXPECT_EQ(f.writes, 0u);
}
TEST(UpdateInstall, FailedCheckpointNeverWritesOrRestarts) {
  update_fixture::FakeUpdate f;
  card.readOnly = true;
  EXPECT_EQ(f.install(), booksync::update::Result::ReceiptStorage);
  EXPECT_EQ(f.writes, 0u);
  EXPECT_EQ(f.restarts, 0u);
}
TEST(UpdateInstall, EveryReceiptCutRetainsProvableBootOrUnknown) {
  update_fixture::FakeUpdate baseline;
  ASSERT_EQ(baseline.install(), booksync::update::Result::Started);
  const auto operations = card.events.size();
  for (size_t cut = 0; cut < operations; ++cut) {
    for (bool eager : {false, true}) {
      update_fixture::FakeUpdate f;
      card.eager = eager;
      card.stop = static_cast<int>(cut);
      try {
        f.install();
      } catch (const PowerCut&) {
      }
      const unsigned writes = f.writes;
      card.restart();
      booksync::update::Update restarted(f.ports, f.device, f.diagnostics);
      if (restarted.recover()) {
        EXPECT_EQ(restarted.receipt().phase, booksync::update::Phase::Interrupted) << cut;
      }
      EXPECT_LE(writes, 1u);
      EXPECT_EQ(f.restarts, 0u);
    }
  }
}
TEST(UpdateInstall, CompatibilityIsRecheckedAfterOffer) {
  update_fixture::FakeUpdate f;
  f.compatible = true;
  ASSERT_TRUE(f.service.canReturn());
  f.compatible = false;
  EXPECT_EQ(f.service.returnPrevious(), booksync::update::Result::Unavailable);
  EXPECT_EQ(f.writes, 0u);
}
TEST(UpdateInstall, UnknownBootAndDamagedReceiptNeverClaimRecovery) {
  update_fixture::FakeUpdate f;
  ASSERT_EQ(f.install(), booksync::update::Result::Started);
  f.boot.digest[0] = 99;
  booksync::update::Update restarted(f.ports, f.device, f.diagnostics);
  EXPECT_FALSE(restarted.recover());
  card.files["/.booksync/update.receipt"][4] ^= 1;
  EXPECT_FALSE(restarted.recover());
}
TEST(UpdateInstall, FailedWriterNeedsBootReconciliation) {
  update_fixture::FakeUpdate f;
  f.writeOk = false;
  EXPECT_EQ(f.install(), booksync::update::Result::Failed);
  EXPECT_EQ(f.service.restart(), booksync::update::Result::Unavailable);
  f.boot = f.image;  // Unknown result may already have selected the target.
  booksync::update::Update restarted(f.ports, f.device, f.diagnostics);
  EXPECT_TRUE(restarted.recover());
  EXPECT_EQ(restarted.receipt().phase, booksync::update::Phase::Complete);
}
TEST(UpdateInstall, HiddenSleepingNotHomeAndDisplayOwnershipDefer) {
  update_fixture::FakeUpdate f;
  f.state.home = false;
  EXPECT_EQ(f.install(), booksync::update::Result::Deferred);
  f.state.home = true;
  f.state.presentation.visible = false;
  EXPECT_EQ(f.install(), booksync::update::Result::Deferred);
  f.state.presentation.visible = true;
  f.state.presentation.sleeping = true;
  EXPECT_EQ(f.install(), booksync::update::Result::Deferred);
  f.state.presentation.sleeping = false;
  f.state.displayReady = false;
  EXPECT_EQ(f.install(), booksync::update::Result::Busy);
  EXPECT_EQ(f.writes, 0u);
}

TEST(UpdateInstall, SameBootWriterCannotAnnounceRecovery) {
  update_fixture::FakeUpdate f;
  ASSERT_EQ(f.install(), booksync::update::Result::Started);
  EXPECT_FALSE(f.service.recover());
  EXPECT_EQ(f.service.receipt().phase, booksync::update::Phase::AwaitBoot);
}
TEST(UpdateInstall, StaleSuccessCannotDescribeAnotherBoot) {
  update_fixture::FakeUpdate f;
  ASSERT_EQ(f.install(), booksync::update::Result::Started);
  f.boot = f.image;
  booksync::update::Update first(f.ports, f.device, f.diagnostics);
  ASSERT_TRUE(first.recover());
  f.boot.digest[0] ^= 4;
  booksync::update::Update later(f.ports, f.device, f.diagnostics);
  EXPECT_FALSE(later.recover());
}

TEST(UpdateInstall, ReadingStartingDuringInspectionDefersFinalAdmission) {
  update_fixture::FakeUpdate f;
  f.readingOnLock = true;
  EXPECT_EQ(f.install(), booksync::update::Result::Deferred);
  EXPECT_EQ(f.writes, 0u);
  EXPECT_FALSE(f.locked);
}
TEST(UpdateInstall, MissingRootUnsignedAndInvalidVersionFailClosed) {
  update_fixture::FakeUpdate f;
  f.release.firmwareSigned = false;
  EXPECT_EQ(f.install(), booksync::update::Result::Trust);
  f.release.firmwareSigned = true;
  auto device = f.device;
  device.root = "";
  booksync::update::Update noRoot(f.ports, device, f.diagnostics);
  EXPECT_EQ(noRoot.install(f.release, {"0.11.4", ""}, booksync::update_pick::Channel::Stable),
            booksync::update::Result::Trust);
  strcpy(f.release.booksyncVersion, "0.12.0-0.test.1");
  EXPECT_EQ(f.install(), booksync::update::Result::Version);
  EXPECT_EQ(f.writes, 0u);
}
TEST(UpdateInstall, ReturnRejectsEveryWrongBoardAndUnverifiedImage) {
  for (auto board : {booksync::update::Board::Pro, booksync::update::Board::Classic, booksync::update::Board::C3}) {
    update_fixture::FakeUpdate f;
    auto device = f.device;
    device.board = board;
    f.previous.board = board;
    f.compatible = true;
    booksync::update::Update service(f.ports, device, f.diagnostics);
    EXPECT_TRUE(service.canReturn());
    f.previous.verified = false;
    EXPECT_FALSE(service.canReturn());
    f.previous.verified = true;
    f.previous.board =
        board == booksync::update::Board::C3 ? booksync::update::Board::Pro : booksync::update::Board::C3;
    EXPECT_FALSE(service.canReturn());
  }
}

TEST(UpdateInstall, ActualPartitionCapacityIsRequired) {
  update_fixture::FakeUpdate f;
  auto device = f.device;
  device.slotBytes = 0;
  booksync::update::Update unavailable(f.ports, device, f.diagnostics);
  EXPECT_EQ(unavailable.install(f.release, {"0.11.4", ""}, booksync::update_pick::Channel::Stable),
            booksync::update::Result::Incomplete);
  device.slotBytes = f.image.size - 1;
  booksync::update::Update tooSmall(f.ports, device, f.diagnostics);
  EXPECT_EQ(tooSmall.install(f.release, {"0.11.4", ""}, booksync::update_pick::Channel::Stable),
            booksync::update::Result::Incomplete);
  EXPECT_EQ(f.writes, 0u);
}

TEST(UpdateInstall, ProvenRecoveryIsReadOnlyOnLaterBoots) {
  update_fixture::FakeUpdate f;
  ASSERT_EQ(f.install(), booksync::update::Result::Started);
  f.boot = f.image;
  booksync::update::Update first(f.ports, f.device, f.diagnostics);
  ASSERT_TRUE(first.recover());
  card.events.clear();
  booksync::update::Update second(f.ports, f.device, f.diagnostics);
  ASSERT_TRUE(second.recover());
  for (const auto& event : card.events) EXPECT_EQ(event.find("write:"), std::string::npos);
  EXPECT_EQ(second.receipt().phase, booksync::update::Phase::Complete);
}
