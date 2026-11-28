#include <gtest/gtest.h>

#include "booksync/ui/catalog/SourceList.h"
using namespace booksync;
namespace {
struct ListLibrary final : Library {
  LibraryWindow data{};
  WindowRequest request{};
  unsigned requests = 0, reads = 0, opens = 0, retires = 0;
  bool accept = true;
  Capabilities capabilities() const override { return {}; }
  Snapshot snapshot() const override { return {}; }
  CommandStatus submit(Intent) override { return CommandStatus::Unavailable; }
  bool requestWindow(WindowRequest r) override {
    request = r;
    ++requests;
    return accept;
  }
  void readWindow(LibraryWindow& out) const override {
    ++const_cast<ListLibrary*>(this)->reads;
    out = data;
  }
  Subscription subscribe() const override { return {data.publication, 1, true}; }
  void retireOpen(BookKey) override { ++retires; }
  CommandStatus open(BookKey) override {
    ++opens;
    return CommandStatus::Accepted;
  }
  void publish(uint16_t count, uint32_t total) {
    ++data.publication;
    data.windowCount = 1;
    data.count = count;
    auto& s = data.windows[0];
    s.request = request;
    s.first = 0;
    s.count = count;
    s.total = total;
    s.availability = Availability::Ready;
    s.bytes = sizeof(WindowSpan) + count * sizeof(BookRow);
  }
};
}  // namespace
TEST(SourceList, OneRequestCopyAndNoPaintWork) {
  ListLibrary lib;
  SourceList list(lib);
  ASSERT_TRUE(list.enter(WindowSource::OnDevice, 10));
  EXPECT_EQ(list.sourceView().viewState, WidgetState::Loading);
  lib.publish(10, 10000);
  ASSERT_TRUE(list.pollSourceList());
  for (unsigned i = 0; i < 40; ++i) {
    EXPECT_EQ(list.sourceView().total, 10000);
    EXPECT_TRUE(list.hasSourceNext());
  }
  EXPECT_EQ(lib.requests, 1);
  EXPECT_EQ(lib.reads, 1);
  EXPECT_FALSE(list.pollSourceList());
}
TEST(SourceList, ActualShortPagesUseRecordedPreviousCursors) {
  ListLibrary lib;
  SourceList list(lib);
  ASSERT_TRUE(list.enter(WindowSource::Recent, 10));
  lib.publish(3, 41);
  ASSERT_TRUE(list.pollSourceList());
  ASSERT_TRUE(list.nextSourcePage());
  EXPECT_EQ(lib.request.offset, 3);
  lib.publish(2, 41);
  ASSERT_TRUE(list.pollSourceList());
  ASSERT_TRUE(list.nextSourcePage());
  EXPECT_EQ(lib.request.offset, 5);
  lib.publish(4, 41);
  ASSERT_TRUE(list.pollSourceList());
  ASSERT_TRUE(list.previousSourcePage());
  EXPECT_EQ(lib.request.offset, 3);
  lib.publish(2, 41);
  ASSERT_TRUE(list.pollSourceList());
  ASSERT_TRUE(list.previousSourcePage());
  EXPECT_EQ(lib.request.offset, 0);
  EXPECT_FALSE(list.hasSourcePrevious());
}
TEST(SourceList, DeepPagingKeepsBoundedActualPreviousHistoryWithoutCappingForward) {
  ListLibrary lib;
  SourceList list(lib);
  ASSERT_TRUE(list.enter(WindowSource::OnDevice, 10));
  for (unsigned i = 0; i < 40; ++i) {
    lib.publish(10, 10000);
    ASSERT_TRUE(list.pollSourceList());
    ASSERT_TRUE(list.nextSourcePage());
  }
  EXPECT_EQ(lib.request.offset, 400);
  for (unsigned i = 0; i < 16; ++i) ASSERT_TRUE(list.previousSourcePage());
  EXPECT_EQ(lib.request.offset, 240);
  EXPECT_FALSE(list.hasSourcePrevious());
}
TEST(SourceList, EmptyUnavailableMalformedAndRetiredValuesCannotOpen) {
  ListLibrary lib;
  SourceList list(lib);
  ASSERT_TRUE(list.enter(WindowSource::OnDevice, 10));
  EXPECT_EQ(list.openSourceRow(0), CommandStatus::Unavailable);
  lib.publish(0, 0);
  ASSERT_TRUE(list.pollSourceList());
  EXPECT_EQ(list.sourceView().viewState, WidgetState::Empty);
  EXPECT_FALSE(list.hasSourceNext());
  EXPECT_EQ(list.openSourceRow(0), CommandStatus::Unavailable);
  lib.publish(3, 41);
  lib.data.windows[0].request.source = WindowSource::Recent;
  ASSERT_TRUE(list.pollSourceList());
  EXPECT_NE(list.sourceView().viewState, WidgetState::Known);
  EXPECT_EQ(list.openSourceRow(0), CommandStatus::Unavailable);
  lib.publish(3, 41);
  lib.data.windows[0].availability = Availability::NotSaved;
  ASSERT_TRUE(list.pollSourceList());
  EXPECT_EQ(list.sourceView().viewState, WidgetState::NotSaved);
  EXPECT_EQ(lib.opens, 0);
}
TEST(SourceList, OpensOnlySelectedReadyKeyAndPreservesSource) {
  ListLibrary lib;
  SourceList list(lib);
  ASSERT_TRUE(list.enter(WindowSource::WantToRead, 10));
  lib.publish(3, 41);
  ASSERT_TRUE(list.pollSourceList());
  EXPECT_EQ(list.sourceKind(), WindowSource::WantToRead);
  EXPECT_EQ(list.openSourceRow(2), CommandStatus::Accepted);
  EXPECT_EQ(lib.opens, 1);
  EXPECT_EQ(list.openSourceRow(3), CommandStatus::Unavailable);
  EXPECT_EQ(lib.opens, 1);
}

TEST(SourceList, PartialMembershipDoesNotBecomeCompleteEmpty) {
  ListLibrary lib;
  SourceList list(lib);
  ASSERT_TRUE(list.enter(WindowSource::WantToRead, 10));
  lib.publish(0, 41);
  ASSERT_TRUE(list.pollSourceList());
  EXPECT_EQ(list.sourceView().viewState, WidgetState::NotSaved);
  EXPECT_FALSE(list.hasSourceNext());
}

TEST(SourceList, BackAndRetiredEpochCancelOnlyItsAcceptedOpenIntent) {
  ListLibrary lib;
  SourceList list(lib);
  ASSERT_TRUE(list.enter(WindowSource::OnDevice, 10));
  lib.publish(3, 41);
  ASSERT_TRUE(list.pollSourceList());
  ASSERT_EQ(list.openSourceRow(0), CommandStatus::Accepted);
  list.retireSourceList();
  EXPECT_EQ(lib.retires, 1);
  EXPECT_EQ(list.openSourceRow(0), CommandStatus::Unavailable);
  ASSERT_TRUE(list.enter(WindowSource::OnDevice, 10));
  lib.publish(3, 41);
  ASSERT_TRUE(list.pollSourceList());
  ASSERT_EQ(list.openSourceRow(0), CommandStatus::Accepted);
  lib.publish(3, 41);
  ++lib.data.provenance.accountEpoch;
  ASSERT_TRUE(list.pollSourceList());
  EXPECT_EQ(lib.retires, 2);
  list.retireSourceList();
  EXPECT_EQ(lib.retires, 2);
}
