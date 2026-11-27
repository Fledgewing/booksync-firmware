#include <gtest/gtest.h>

#include "booksync/ui/WidgetHost.h"
using namespace booksync;
namespace {
struct Fake final : Library {
  HomePage page{};
  LibraryWindow data{};
  WindowBatchRequest requested{};
  unsigned batches = 0, reads = 0, opens = 0;
  BookKey opened{};
  Capabilities capabilities() const override { return {}; }
  Snapshot snapshot() const override { return {}; }
  Subscription subscribe() const override { return {data.publication, 1, true}; }
  HomePage homePage() const override { return page; }
  bool requestHomePage(uint8_t) override { return true; }
  bool requestBatch(WindowBatchRequest v) override {
    requested = v;
    ++batches;
    return true;
  }
  void readWindow(LibraryWindow& out) const override {
    ++const_cast<Fake*>(this)->reads;
    out = data;
  }
  CommandStatus submit(Intent) override {
    ++opens;
    return CommandStatus::Accepted;
  }
  CommandStatus open(BookKey key) override {
    opened = key;
    ++opens;
    return CommandStatus::Accepted;
  }
  Fake() {
    page.revision = 1;
    page.count = 3;
    page.widgets[0].kind = WidgetKind::Continue;
    page.widgets[1].kind = WidgetKind::Shelf;
    page.widgets[2].kind = WidgetKind::Browse;
    for (unsigned i = 0; i < 3; ++i) {
      page.widgets[i].w = 2;
      page.widgets[i].h = 2;
    }
  }
  void publish() {
    data.publication++;
    data.windowCount = requested.count;
    data.count = 0;
    for (unsigned i = 0; i < requested.count; ++i) {
      auto& s = data.windows[i];
      s.request = requested.windows[i];
      s.first = data.count;
      s.availability = Availability::Ready;
      s.count = 1;
      s.bytes = sizeof(WindowSpan) + sizeof(BookRow);
      data.rows[data.count++].title[0] = 'A';
    }
  }
};
class WidgetStates : public testing::TestWithParam<WidgetKind> {};
TEST_P(WidgetStates, Known) {
  Fake f;
  f.page.count = 1;
  f.page.widgets[0].kind = GetParam();
  WidgetHost h(f);
  h.enter();
  h.pollWidgets();
  f.publish();
  h.pollWidgets();
  EXPECT_EQ(h.view(0).viewState, WidgetState::Known);
}
TEST_P(WidgetStates, Empty) {
  Fake f;
  f.page.count = 1;
  f.page.widgets[0].kind = GetParam();
  WidgetHost h(f);
  h.enter();
  h.pollWidgets();
  f.publish();
  f.data.windows[0].count = 0;
  h.pollWidgets();
  EXPECT_EQ(h.view(0).viewState, WidgetState::Empty);
}
TEST_P(WidgetStates, Error) {
  Fake f;
  f.page.count = 1;
  f.page.widgets[0].kind = GetParam();
  WidgetHost h(f);
  h.enter();
  h.pollWidgets();
  f.publish();
  f.data.windows[0].availability = Availability::Unavailable;
  h.pollWidgets();
  EXPECT_EQ(h.view(0).viewState, WidgetState::Error);
}
TEST_P(WidgetStates, Oversize) {
  Fake f;
  f.page.count = 1;
  f.page.widgets[0].kind = GetParam();
  WidgetHost h(f);
  h.enter();
  h.pollWidgets();
  f.publish();
  f.data.windows[0].bytes = 16385;
  h.pollWidgets();
  EXPECT_EQ(h.view(0).viewState, WidgetState::Oversize);
}
INSTANTIATE_TEST_SUITE_P(StarterWidgets, WidgetStates,
                         testing::Values(WidgetKind::Continue, WidgetKind::Shelf, WidgetKind::Browse));
TEST(WidgetHost, OneBatchAndReadPerPublicationWithIndependentErrors) {
  Fake f;
  WidgetHost h(f);
  h.enter();
  h.pollWidgets();
  EXPECT_EQ(f.batches, 1u);
  f.publish();
  f.data.windows[0].availability = Availability::Unavailable;
  f.data.windows[1].bytes = 16385;
  h.pollWidgets();
  EXPECT_EQ(h.view(0).viewState, WidgetState::Error);
  EXPECT_EQ(h.view(1).viewState, WidgetState::Oversize);
  EXPECT_EQ(h.view(2).viewState, WidgetState::Known);
  const auto reads = f.reads;
  for (int i = 0; i < 10; ++i) {
    h.view(0);
    h.view(1);
    h.view(2);
  }
  EXPECT_EQ(f.reads, reads);
  EXPECT_EQ(f.batches, 1u);
}
TEST(WidgetHost, RetryKeepsSiblingSnapshotUntilNextPublication) {
  Fake f;
  WidgetHost h(f);
  h.enter();
  h.pollWidgets();
  f.publish();
  f.data.windows[0].availability = Availability::Unavailable;
  h.pollWidgets();
  EXPECT_TRUE(h.retryWidget(0));
  EXPECT_EQ(f.batches, 2u);
  EXPECT_EQ(h.view(2).viewState, WidgetState::Known);
  EXPECT_FALSE(h.retryWidget(2));
}
TEST(WidgetHost, BoundsAndStaleRequestsCannotOpen) {
  Fake f;
  WidgetHost h(f);
  h.enter();
  h.pollWidgets();
  f.publish();
  f.data.windows[0].first = LibraryRows;
  h.pollWidgets();
  EXPECT_EQ(h.view(0).viewState, WidgetState::Error);
  EXPECT_EQ(h.dispatchWidget(0), CommandStatus::Unavailable);
  EXPECT_EQ(f.opens, 0u);
  f.publish();
  f.data.windows[1].request.offset++;
  h.pollWidgets();
  EXPECT_EQ(h.view(1).viewState, WidgetState::Loading);
}
TEST(WidgetHost, UnknownWidgetsAndVersionsNeverQuery) {
  Fake f;
  f.page.count = 0;
  WidgetHost h(f);
  h.enter();
  h.pollWidgets();
  EXPECT_EQ(f.batches, 0u);
}
TEST(WidgetHost, PageSwitchClearsOldRowsAndBatchesOnce) {
  Fake f;
  WidgetHost h(f);
  h.enter();
  h.pollWidgets();
  f.publish();
  h.pollWidgets();
  h.enter(1);
  EXPECT_EQ(h.dispatchWidget(1), CommandStatus::Unavailable);
  f.page.page = 1;
  h.pollWidgets();
  EXPECT_EQ(f.batches, 2u);
  EXPECT_EQ(h.view(1).viewState, WidgetState::Loading);
}
TEST(WidgetHost, ShelfHitSelectsTheDisplayedBookWithoutAnotherRead) {
  Fake f;
  f.page.count = 1;
  f.page.widgets[0].kind = WidgetKind::Shelf;
  WidgetHost h(f);
  h.enter();
  h.pollWidgets();
  f.publish();
  f.data.count = f.data.windows[0].count = 3;
  f.data.windows[0].bytes = sizeof(WindowSpan) + 3 * sizeof(BookRow);
  for (unsigned i = 0; i < 3; ++i) {
    f.data.rows[i].title[0] = 'A';
    f.data.rows[i].key.ordinal = 20 + i;
  }
  h.pollWidgets();
  const WidgetBox box{450, 180, 24, false, true};
  EXPECT_EQ(h.widgetRows(0, box), 3);
  WidgetCell cell;
  ASSERT_TRUE(h.widgetCell(0, box, 1, cell));
  EXPECT_GE(cell.width, 50);
  EXPECT_GE(cell.height, 50);
  const auto row = h.hitWidgetRow(0, box, cell.x + cell.width / 2, cell.y + cell.height / 2);
  EXPECT_EQ(row, 1);
  EXPECT_EQ(h.dispatchWidget(0, row), CommandStatus::Accepted);
  EXPECT_EQ(f.opened.ordinal, 21u);
  EXPECT_EQ(h.hitWidgetRow(0, box, 20, 5), 255);
  EXPECT_EQ(f.batches, 1u);
  EXPECT_EQ(f.reads, 1u);
}
TEST(WidgetHost, TextShelfFocusStopsAtTheLastVisibleRow) {
  Fake f;
  f.page.count = 1;
  f.page.textOnly = true;
  f.page.widgets[0].kind = WidgetKind::Shelf;
  WidgetHost h(f);
  h.enter();
  h.pollWidgets();
  f.publish();
  f.data.count = f.data.windows[0].count = 3;
  f.data.windows[0].bytes = sizeof(WindowSpan) + 3 * sizeof(BookRow);
  for (unsigned i = 0; i < 3; ++i) f.data.rows[i].title[0] = 'A';
  h.pollWidgets();
  const WidgetBox box{450, 100, 20, true, false};
  EXPECT_EQ(h.widgetRows(0, box), 1);
  WidgetCell cell;
  ASSERT_TRUE(h.widgetCell(0, box, 0, cell));
  EXPECT_FALSE(cell.cover);
  EXPECT_FALSE(h.widgetCell(0, box, 1, cell));
}
}  // namespace
