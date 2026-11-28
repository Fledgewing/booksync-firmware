#include <gtest/gtest.h>

#include "booksync/ui/WidgetHost.h"
using namespace booksync;
namespace {
class ShelfLibrary final : public Library {
 public:
  HomePage page{};
  LibraryWindow data{};
  WindowBatchRequest batch{};
  unsigned reads = 0, browses = 0, opens = 0;
  WindowSource selected = WindowSource::Unknown;
  BookKey opened{};
  ShelfLibrary() {
    page.revision = 1;
    page.rows = 6;
    page.count = 1;
    page.widgets[0].kind = WidgetKind::Shelf;
    page.widgets[0].source = WindowSource::Recent;
  }
  Capabilities capabilities() const override { return {}; }
  Snapshot snapshot() const override { return {}; }
  CommandStatus submit(Intent) override { return CommandStatus::Unavailable; }
  HomePage homePage() const override { return page; }
  bool requestHomePage(uint8_t) override { return true; }
  bool requestBatch(WindowBatchRequest r) override {
    batch = r;
    return true;
  }
  Subscription subscribe() const override { return {data.publication, 1, true}; }
  void readWindow(LibraryWindow& out) const override {
    ++const_cast<ShelfLibrary*>(this)->reads;
    out = data;
  }
  CommandStatus browseSource(WindowSource s) override {
    ++browses;
    selected = s;
    return CommandStatus::Accepted;
  }
  CommandStatus open(BookKey key) override {
    ++opens;
    opened = key;
    return CommandStatus::Accepted;
  }
  void publish(uint16_t count, uint32_t total) {
    ++data.publication;
    data.count = count;
    data.windowCount = 1;
    auto& span = data.windows[0];
    span = {};
    span.request = batch.windows[0];
    span.count = count;
    span.total = total;
    span.bytes = sizeof(WindowSpan) + count * sizeof(BookRow);
    span.availability = Availability::Ready;
    for (uint16_t i = 0; i < count; ++i) {
      data.rows[i].title[0] = 'A';
      data.rows[i].key.ordinal = 20 + i;
    }
  }
};
}  // namespace
TEST(BookListHost, HeaderAndCoverAreIndependentLibraryIntentsWithoutExtraCopies) {
  ShelfLibrary lib;
  WidgetHost host(lib);
  host.enter();
  host.pollWidgets();
  EXPECT_EQ(lib.batch.windows[0].limit, 4);
  EXPECT_EQ(lib.batch.windows[0].presentation, WindowPresentation::Preview);
  lib.publish(4, 10000);
  ASSERT_TRUE(host.pollWidgets());
  const WidgetBox box{456, 224, 26, false, true};
  EXPECT_EQ(host.widgetRows(0, box), 5);
  EXPECT_EQ(host.hitWidgetRow(0, box, 400, 20), 0);
  EXPECT_EQ(host.dispatchWidget(0, 0), CommandStatus::Accepted);
  EXPECT_EQ(lib.browses, 1);
  EXPECT_EQ(lib.selected, WindowSource::Recent);
  WidgetCell cell;
  ASSERT_TRUE(host.widgetCell(0, box, 2, cell));
  auto hit = host.hitWidgetRow(0, box, cell.x + 4, cell.y + 4);
  EXPECT_EQ(hit, 3);
  EXPECT_EQ(host.dispatchWidget(0, hit), CommandStatus::Accepted);
  EXPECT_EQ(lib.opened.ordinal, 22);
  EXPECT_EQ(lib.reads, 1);
}
TEST(BookListHost, CompleteEmptyCanOpenItsZeroCountDestinationButUnavailableCannot) {
  ShelfLibrary lib;
  WidgetHost host(lib);
  host.enter();
  host.pollWidgets();
  lib.publish(0, 0);
  host.pollWidgets();
  EXPECT_EQ(host.dispatchWidget(0), CommandStatus::Accepted);
  EXPECT_EQ(lib.browses, 1);
  lib.publish(0, 0);
  lib.data.windows[0].availability = Availability::Off;
  host.pollWidgets();
  EXPECT_EQ(host.view(0).viewState, WidgetState::Off);
  EXPECT_EQ(host.dispatchWidget(0), CommandStatus::Unavailable);
  EXPECT_EQ(lib.browses, 1);
}
TEST(BookListHost, X3KeepsItsExistingThreeTextRowsAndDirectBookSelection) {
  ShelfLibrary lib;
  lib.page.textOnly = true;
  WidgetHost host(lib);
  host.enter();
  host.pollWidgets();
  EXPECT_EQ(lib.batch.windows[0].limit, 3);
  EXPECT_EQ(lib.batch.windows[0].presentation, WindowPresentation::Full);
  lib.publish(3, 41);
  host.pollWidgets();
  EXPECT_EQ(host.dispatchWidget(0, 0), CommandStatus::Accepted);
  EXPECT_EQ(lib.opens, 1);
  EXPECT_EQ(lib.browses, 0);
  EXPECT_EQ(lib.opened.ordinal, 20);
}
