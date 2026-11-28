#include <gtest/gtest.h>

#include "booksync/ui/WidgetHost.h"
using namespace booksync;
namespace {
class PersonalLibrary final : public Library {
 public:
  HomePage page{};
  LibraryWindow data{};
  WindowBatchRequest batch{};
  unsigned shuffles = 0, browses = 0, opens = 0;
  BookKey selected{};
  PersonalLibrary() {
    page.revision = 1;
    page.rows = 6;
    page.count = 2;
    page.widgets[0].kind = WidgetKind::Discover;
    page.widgets[1].kind = WidgetKind::Recommended;
  }
  Capabilities capabilities() const override { return {}; }
  Snapshot snapshot() const override { return {}; }
  CommandStatus submit(Intent) override { return CommandStatus::Unsupported; }
  HomePage homePage() const override { return page; }
  bool requestHomePage(uint8_t) override { return true; }
  Subscription subscribe() const override { return {data.publication}; }
  bool requestBatch(WindowBatchRequest requested) override {
    batch = requested;
    data.windowCount = requested.count;
    ++data.publication;
    for (uint8_t i = 0; i < requested.count; ++i) {
      auto& span = data.windows[i];
      span.request = requested.windows[i];
      span.first = data.count;
      span.count = i ? 1 : 4;
      span.total = i ? 19 : 37;
      span.availability = Availability::Ready;
      span.bytes = sizeof(WindowSpan) + span.count * sizeof(BookRow);
      for (uint8_t n = 0; n < span.count; ++n) {
        data.rows[data.count].key.ordinal = data.count;
        data.rows[data.count++].title[0] = 'N';
      }
    }
    return true;
  }
  void readWindow(LibraryWindow& out) const override { out = data; }
  CommandStatus anotherDiscover() override {
    ++shuffles;
    return CommandStatus::Accepted;
  }
  CommandStatus browseSource(WindowSource source) override {
    EXPECT_EQ(source, WindowSource::Random);
    ++browses;
    return CommandStatus::Accepted;
  }
  CommandStatus open(BookKey book) override {
    selected = book;
    ++opens;
    return CommandStatus::Accepted;
  }
};
TEST(PersonalHost, OneBatchExplicitFourRowsAndSeparateHeaderActions) {
  PersonalLibrary lib;
  WidgetHost host(lib);
  host.enter();
  host.pollWidgets();
  host.pollWidgets();
  ASSERT_EQ(lib.batch.count, 2);
  EXPECT_EQ(lib.batch.windows[0].limit, 4);
  EXPECT_EQ(lib.batch.windows[1].limit, 1);
  EXPECT_EQ(host.view(0).count, 4);
  WidgetBox box{456, 224, 26, false, true};
  EXPECT_EQ(host.widgetRows(0, box), 6);
  EXPECT_EQ(host.hitWidgetRow(0, box, 410, 20), 0);
  EXPECT_EQ(host.hitWidgetRow(0, box, 340, 20), 1);
  EXPECT_EQ(host.dispatchWidget(0, 0), CommandStatus::Accepted);
  EXPECT_EQ(host.dispatchWidget(0, 1), CommandStatus::Accepted);
  EXPECT_EQ(host.dispatchWidget(0, 5), CommandStatus::Accepted);
  EXPECT_EQ(lib.selected.ordinal, 3u);
  EXPECT_EQ(host.dispatchWidget(1), CommandStatus::Accepted);
  EXPECT_EQ(lib.selected.ordinal, 4u);
  EXPECT_EQ(lib.shuffles, 1u);
  EXPECT_EQ(lib.browses, 1u);
}
TEST(PersonalHost, NoNewRequestsOrActionsOnX3) {
  PersonalLibrary lib;
  lib.page.textOnly = true;
  WidgetHost host(lib);
  host.enter();
  host.pollWidgets();
  EXPECT_EQ(host.page().count, 0);
  EXPECT_EQ(lib.batch.count, 0);
  EXPECT_EQ(host.dispatchWidget(0, 1), CommandStatus::Unavailable);
  EXPECT_EQ(lib.shuffles + lib.browses + lib.opens, 0u);
}
TEST(PersonalHost, OversizedDiscoveryDoesNotDisableRecommended) {
  PersonalLibrary lib;
  WidgetHost host(lib);
  host.enter();
  host.pollWidgets();
  lib.data.windows[0].bytes = LibraryBytes + 1;
  ++lib.data.publication;
  host.pollWidgets();
  EXPECT_EQ(host.view(0).viewState, WidgetState::Oversize);
  EXPECT_EQ(host.view(1).viewState, WidgetState::Known);
  EXPECT_EQ(host.dispatchWidget(0, 1), CommandStatus::Unavailable);
  EXPECT_EQ(host.dispatchWidget(1), CommandStatus::Accepted);
}
}  // namespace
