#include <gtest/gtest.h>

#include "booksync/services/DeviceBridge.h"
using namespace booksync;
namespace {
struct Fixture : testing::Test {
  DeviceBridge bridge;
  DeviceSnapshot values{};
  DeviceCommand applied{};
  unsigned calls = 0;
  static bool apply(void* context, DeviceCommand command) {
    auto& f = *static_cast<Fixture*>(context);
    ++f.calls;
    f.applied = command;
    return true;
  }
  void SetUp() override {
    values.battery.supported = values.battery.percentKnown = true;
    values.light.supported = values.light.known = true;
    values.light.level = 40;
    values.controls.known = true;
    bridge.publish(values);
  }
  DeviceCommand command(DeviceAction action, uint8_t value) { return {bridge.snapshot().revision, action, value}; }
};
TEST_F(Fixture, UnpublishedIsUnavailable) {
  DeviceBridge cold;
  EXPECT_EQ(cold.submit({1, DeviceAction::Night, 1}), CommandStatus::Unavailable);
  EXPECT_EQ(cold.snapshot().revision, 0u);
}
TEST_F(Fixture, KnownZeroAndFalseRemainKnown) {
  const auto out = bridge.snapshot();
  EXPECT_TRUE(out.battery.percentKnown);
  EXPECT_EQ(out.battery.percent, 0);
  EXPECT_TRUE(out.controls.known);
  EXPECT_FALSE(out.controls.night);
  EXPECT_FALSE(out.light.on);
}
TEST_F(Fixture, SamplePublicationHasNoControlSideEffectsAndCoalesces) {
  const auto version = bridge.snapshot().revision;
  bridge.publish(values);
  EXPECT_EQ(bridge.snapshot().revision, version);
  EXPECT_FALSE(bridge.work({this, apply}));
  EXPECT_EQ(calls, 0u);
}
TEST_F(Fixture, EveryActionUsesAbsoluteTargetAndOneMainLoopApply) {
  for (auto action : {DeviceAction::Night, DeviceAction::Orientation, DeviceAction::Wifi, DeviceAction::LightOn,
                      DeviceAction::LightLevel}) {
    ASSERT_EQ(bridge.submit(command(action, 1)), CommandStatus::Accepted);
    EXPECT_EQ(calls, 0u);
    EXPECT_TRUE(bridge.work({this, apply}));
    EXPECT_EQ(calls, 1u);
    EXPECT_EQ(applied.action, action);
    EXPECT_EQ(applied.value, 1);
    EXPECT_FALSE(bridge.work({this, apply}));
    calls = 0;
  }
}
TEST_F(Fixture, NoOpDoesNotQueueOrWrite) {
  EXPECT_EQ(bridge.submit(command(DeviceAction::Night, 0)), CommandStatus::Accepted);
  EXPECT_FALSE(bridge.work({this, apply}));
  EXPECT_EQ(calls, 0u);
}
TEST_F(Fixture, BrightnessAtTheSavedLevelStillWakesAnOffLight) {
  EXPECT_EQ(bridge.submit(command(DeviceAction::LightLevel, 40)), CommandStatus::Accepted);
  EXPECT_TRUE(bridge.work({this, apply}));
  EXPECT_EQ(calls, 1u);
}
TEST_F(Fixture, PendingIsNotReplaced) {
  EXPECT_EQ(bridge.submit(command(DeviceAction::Night, 1)), CommandStatus::Accepted);
  EXPECT_EQ(bridge.submit(command(DeviceAction::Orientation, 2)), CommandStatus::Unavailable);
  EXPECT_TRUE(bridge.work({this, apply}));
  EXPECT_EQ(applied.action, DeviceAction::Night);
}
TEST_F(Fixture, StaleRevisionNeverAppliesToNewValues) {
  auto old = command(DeviceAction::Night, 1);
  ++values.battery.percent;
  bridge.publish(values);
  EXPECT_EQ(bridge.submit(old), CommandStatus::Unavailable);
  EXPECT_FALSE(bridge.work({this, apply}));
}
TEST_F(Fixture, ChangedValueRetiresAlreadyQueuedCommand) {
  bridge.submit(command(DeviceAction::Night, 1));
  values.controls.orientation = 1;
  bridge.publish(values);
  EXPECT_FALSE(bridge.work({this, apply}));
  EXPECT_EQ(calls, 0u);
}
TEST_F(Fixture, ClassicCannotQueueAnyFrontlightCommandEvenWhenOff) {
  values.light.supported = false;
  bridge.publish(values);
  EXPECT_EQ(bridge.submit(command(DeviceAction::LightOn, 1)), CommandStatus::Unsupported);
  EXPECT_EQ(bridge.submit(command(DeviceAction::LightLevel, 50)), CommandStatus::Unsupported);
  EXPECT_FALSE(bridge.snapshot().light.known);
  EXPECT_EQ(bridge.snapshot().light.level, 0);
}
TEST_F(Fixture, MissingValuesAreUnavailableRatherThanZero) {
  values.light.known = false;
  values.controls.known = false;
  values.battery.percentKnown = false;
  bridge.publish(values);
  EXPECT_EQ(bridge.submit(command(DeviceAction::LightLevel, 1)), CommandStatus::Unavailable);
  EXPECT_EQ(bridge.submit(command(DeviceAction::Night, 1)), CommandStatus::Unavailable);
  EXPECT_FALSE(bridge.snapshot().battery.percentKnown);
}
TEST_F(Fixture, BoundsAndUnknownCommandsNeverReachPorts) {
  for (auto entry : {std::pair{DeviceAction::Night, 2}, std::pair{DeviceAction::Wifi, 2},
                     std::pair{DeviceAction::Orientation, 4}, std::pair{DeviceAction::LightOn, 2},
                     std::pair{DeviceAction::LightLevel, 101}, std::pair{static_cast<DeviceAction>(255), 0}})
    EXPECT_NE(bridge.submit(command(entry.first, entry.second)), CommandStatus::Accepted);
  EXPECT_FALSE(bridge.work({this, apply}));
}
TEST_F(Fixture, NetworkOwnershipCannotBeStolen) {
  values.controls.networkBusy = true;
  bridge.publish(values);
  EXPECT_EQ(bridge.submit(command(DeviceAction::Wifi, 1)), CommandStatus::Unavailable);
  EXPECT_EQ(bridge.submit(command(DeviceAction::Night, 1)), CommandStatus::Accepted);
}
TEST_F(Fixture, RetireDiscardsQueueWithoutNativeWrites) {
  bridge.submit(command(DeviceAction::Night, 1));
  bridge.retire();
  EXPECT_FALSE(bridge.work({this, apply}));
  EXPECT_EQ(calls, 0u);
}
TEST_F(Fixture, MissingPortAndFailedApplyDoNotRetryBlindly) {
  bridge.submit(command(DeviceAction::Night, 1));
  EXPECT_FALSE(bridge.work({}));
  EXPECT_FALSE(bridge.work({this, apply}));
  bridge.submit(command(DeviceAction::Night, 1));
  EXPECT_FALSE(bridge.work({nullptr, [](void*, DeviceCommand) { return false; }}));
  EXPECT_FALSE(bridge.work({this, apply}));
}
TEST_F(Fixture, InvalidHardwareSamplesStayUnknown) {
  values.battery.percent = 101;
  values.light.level = 101;
  values.controls.orientation = 4;
  bridge.publish(values);
  const auto out = bridge.snapshot();
  EXPECT_FALSE(out.battery.percentKnown);
  EXPECT_FALSE(out.light.known);
  EXPECT_FALSE(out.controls.known);
}
TEST_F(Fixture, OldHomeRevisionCannotReplayAfterRetirement) {
  const auto old = bridge.snapshot().revision;
  bridge.retire();
  EXPECT_EQ(bridge.snapshot().revision, 0);
  bridge.publish(values);
  EXPECT_GT(bridge.snapshot().revision, old);
  EXPECT_EQ(bridge.submit({old, DeviceAction::Night, 1}), CommandStatus::Unavailable);
  EXPECT_FALSE(bridge.work({this, apply}));
}
TEST_F(Fixture, FailedApplyPublishesAnOutcomeAndNeverClaimsSuccess) {
  auto command = DeviceCommand{bridge.snapshot().revision, DeviceAction::Night, 1};
  EXPECT_EQ(bridge.submit(command), CommandStatus::Accepted);
  EXPECT_EQ(bridge.result().outcome, DeviceOutcome::Pending);
  EXPECT_FALSE(bridge.work({nullptr, [](void*, DeviceCommand) { return false; }}));
  EXPECT_EQ(bridge.result().outcome, DeviceOutcome::Failed);
  EXPECT_EQ(bridge.result().command.revision, command.revision);
  EXPECT_EQ(bridge.snapshot().controls.night, false);
  bridge.retire();
  EXPECT_EQ(bridge.result().outcome, DeviceOutcome::None);
}

}  // namespace
