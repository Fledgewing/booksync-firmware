#include <HalStorage.h>
#include <gtest/gtest.h>

#include <string>

#include "booksync/services/LayoutEditor.h"
#include "booksync/storage/LayoutStore.h"
#include "booksync/ui/EditorAddRemove.h"
using namespace booksync;
namespace {
constexpr storage::PublishPaths paths{"/layout", "/layout.tmp", "/layout.bak"};
std::string bytes(LayoutInput in) {
  std::string out(in.bytes, '\0');
  EXPECT_TRUE(in.read(in.context, 0, out.data(), out.size()));
  return out;
}
std::string original() { return bytes(Layout::defaults()); }
LayoutEdit request(LayoutEditKind kind, uint32_t revision = 1) {
  LayoutEdit edit;
  edit.kind = kind;
  edit.revision = revision;
  edit.widget = WidgetKind::Browse;
  return edit;
}
}  // namespace
TEST(LayoutEditor, AddRemoveResetAndRestart) {
  for (const char* device : {"x4", "x3"}) {
    card = {};
    LayoutEditor editor;
    auto raw = original();
    auto edit = request(LayoutEditKind::Add);
    ASSERT_EQ(editor.prepare(LayoutInput::memory(raw.data(), raw.size()), device, edit), LayoutEditStatus::Saved);
    storage::LayoutStore store(paths, device);
    ASSERT_TRUE(store.save(editor.output()));
    HomePage page;
    ASSERT_TRUE(store.project(0, page));
    EXPECT_EQ(page.count, 4);
    EXPECT_EQ(page.revision, 2u);
    raw = card.files[paths.current];
    edit = request(LayoutEditKind::Remove, 2);
    edit.index = 1;
    ASSERT_EQ(editor.prepare(LayoutInput::memory(raw.data(), raw.size()), device, edit), LayoutEditStatus::Saved);
    ASSERT_TRUE(store.save(editor.output()));
    storage::LayoutStore restarted(paths, device);
    ASSERT_EQ(restarted.load(), storage::LayoutSource::Current);
    ASSERT_TRUE(restarted.project(0, page));
    EXPECT_EQ(page.count, 3);
    EXPECT_EQ(page.widgets[1].kind, WidgetKind::Browse);
    raw = card.files[paths.current];
    edit = request(LayoutEditKind::Reset, 3);
    ASSERT_EQ(editor.prepare(LayoutInput::memory(raw.data(), raw.size()), device, edit), LayoutEditStatus::Saved);
    ASSERT_TRUE(store.save(editor.output()));
    ASSERT_TRUE(store.project(0, page));
    EXPECT_EQ(page.count, 3);
    EXPECT_EQ(page.revision, 4u);
    EXPECT_EQ(page.widgets[1].kind, WidgetKind::Shelf);
    EXPECT_EQ(card.maxHandles, 1);
  }
}
TEST(LayoutEditor, RejectsStaleAndInvalidSelectionWithoutOutput) {
  LayoutEditor editor;
  auto raw = original();
  auto edit = request(LayoutEditKind::Remove, 7);
  EXPECT_EQ(editor.prepare(LayoutInput::memory(raw.data(), raw.size()), "x4", edit), LayoutEditStatus::Stale);
  EXPECT_EQ(editor.output().bytes, 0u);
  edit.revision = 1;
  edit.index = 99;
  EXPECT_EQ(editor.prepare(LayoutInput::memory(raw.data(), raw.size()), "x4", edit), LayoutEditStatus::Invalid);
  edit.page = 99;
  EXPECT_EQ(editor.prepare(LayoutInput::memory(raw.data(), raw.size()), "x4", edit), LayoutEditStatus::Invalid);
  EXPECT_EQ(editor.output().bytes, 0u);
}
TEST(LayoutEditor, UnknownBytesAndClassOverridesSurviveEdits) {
  LayoutEditor editor;
  auto raw = original();
  raw.insert(raw.size() - 1, ",\"future\":{\"payload\":\"" + std::string(100000, 'q') + "\"}");
  raw.replace(raw.find("\"settings\":{}"), 13, "\"settings\":{\"unrecognized\":[null,1.0]}");
  const std::string opaque = "\"future\":{\"payload\":\"" + std::string(100000, 'q') + "\"}";
  auto edit = request(LayoutEditKind::Add);
  ASSERT_EQ(editor.prepare(LayoutInput::memory(raw.data(), raw.size()), "x4", edit), LayoutEditStatus::Saved);
  auto changed = bytes(editor.output());
  EXPECT_NE(changed.find(opaque), std::string::npos);
  EXPECT_NE(changed.find("\"unrecognized\":[null,1.0]"), std::string::npos);
  Layout parser;
  for (const char* device : {"android-tablet", "x4", "x3"})
    EXPECT_TRUE(parser.validate(LayoutInput::memory(changed.data(), changed.size()), device));
  raw = changed;
  edit = request(LayoutEditKind::Remove, 2);
  edit.index = 0;
  ASSERT_EQ(editor.prepare(LayoutInput::memory(raw.data(), raw.size()), "x4", edit), LayoutEditStatus::Saved);
  changed = bytes(editor.output());
  EXPECT_NE(changed.find(opaque), std::string::npos);
}
TEST(LayoutEditor, EveryEditPublicationCutRetainsACompleteLayout) {
  for (auto kind : {LayoutEditKind::Add, LayoutEditKind::Remove, LayoutEditKind::Reset}) {
    LayoutEditor editor;
    auto raw = original();
    ASSERT_EQ(editor.prepare(LayoutInput::memory(raw.data(), raw.size()), "x4", request(kind)),
              LayoutEditStatus::Saved);
    const auto next = bytes(editor.output());
    auto seed = [&] {
      card = {};
      card.files[paths.current] = raw;
      card.disk = card.files;
    };
    seed();
    storage::LayoutStore store(paths, "x4");
    ASSERT_TRUE(store.save(LayoutInput::memory(next.data(), next.size())));
    // Exercise every mutating publication boundary, including eager metadata.
    const auto events = card.events;
    for (bool eager : {false, true})
      for (size_t cut = 0; cut < events.size(); ++cut) {
        if (events[cut] == "read" || events[cut] == "seek" || events[cut] == "open" || events[cut] == "close") continue;
        seed();
        card.eager = eager;
        card.stop = cut;
        EXPECT_THROW(store.save(LayoutInput::memory(next.data(), next.size())), PowerCut);
        card.restart();
        storage::LayoutStore recovered(paths, "x4");
        ASSERT_NE(recovered.load(), storage::LayoutSource::Default);
        const auto& saved = card.files[recovered.selectedPath()];
        EXPECT_TRUE(saved == raw || saved == next);
      }
    for (bool full : {false, true}) {
      seed();
      card.full = full;
      card.readOnly = !full;
      EXPECT_FALSE(store.save(LayoutInput::memory(next.data(), next.size())));
      EXPECT_EQ(card.files[paths.current], raw);
    }
  }
}
TEST(LayoutEditor, WorkspaceIsBoundedWithoutPayloadAllocation) { EXPECT_LE(sizeof(LayoutEditor), 3072u); }
TEST(LayoutEditor, ProductionStoreEditsAndReadHandlesCloseBeforeRename) {
  card = {};
  card.files[paths.current] = original();
  card.disk = card.files;
  storage::LayoutStore store(paths, "x4");
  LayoutEditor editor;
  EXPECT_EQ(store.edit(editor, request(LayoutEditKind::Add)), LayoutEditStatus::Saved);
  EXPECT_EQ(card.handles, 0);
  EXPECT_LE(card.maxHandles, 2);  // Source reader plus staging writer; never two source readers.
  HomePage page;
  ASSERT_TRUE(store.project(0, page));
  EXPECT_EQ(page.revision, 2u);
  EXPECT_EQ(page.count, 4);
  EXPECT_EQ(store.edit(editor, request(LayoutEditKind::Remove)), LayoutEditStatus::Stale);
  EXPECT_EQ(card.handles, 0);
  EXPECT_EQ(store.edit(editor, request(LayoutEditKind::Remove, 2)), LayoutEditStatus::Saved);
  EXPECT_EQ(card.handles, 0);
}
TEST(LayoutEditor, AddBudgetAndAllClassesHaveIndependentGeometry) {
  LayoutEditor editor;
  auto raw = original();
  ASSERT_EQ(editor.prepare(LayoutInput::memory(raw.data(), raw.size()), "x4", request(LayoutEditKind::Add)),
            LayoutEditStatus::Saved);
  raw = bytes(editor.output());
  auto status = LayoutEditStatus::Saved;
  for (unsigned revision = 2; revision < 16 && status == LayoutEditStatus::Saved; ++revision) {
    status = editor.prepare(LayoutInput::memory(raw.data(), raw.size()), "x4", request(LayoutEditKind::Add, revision));
    if (status == LayoutEditStatus::Saved) raw = bytes(editor.output());
  }
  EXPECT_EQ(status, LayoutEditStatus::NoSpace);
  EXPECT_EQ(editor.output().bytes, 0u);
  raw = original();
  // The x3 projection is full, even though the current x4 page has space.
  raw.replace(raw.find("\"x3\":{\"cols\":2,\"rows\":7}"), 24, "\"x3\":{\"cols\":2,\"rows\":5}");
  EXPECT_EQ(editor.prepare(LayoutInput::memory(raw.data(), raw.size()), "x4", request(LayoutEditKind::Add)),
            LayoutEditStatus::NoSpace);
}
namespace {
class EditorFixture final : public Library {
 public:
  HomePage page{};
  LayoutEdit captured{};
  LayoutEditResult result{};
  unsigned writes = 0;
  Capabilities capabilities() const override { return {}; }
  Snapshot snapshot() const override { return {}; }
  HomePage homePage() const override { return page; }
  CommandStatus submit(Intent) override { return CommandStatus::Unavailable; }
  CommandStatus editLayout(LayoutEdit edit) override {
    captured = edit;
    ++writes;
    result.status = LayoutEditStatus::Pending;
    return CommandStatus::Accepted;
  }
  LayoutEditResult layoutEditResult() const override { return result; }
};
}  // namespace
TEST(EditorAddRemove, SelectionResetCancelAndBackDoNotWrite) {
  EditorFixture lib;
  lib.page.revision = 7;
  lib.page.count = 3;
  EditorAddRemove ui(lib);
  ui.enter();
  ui.chooseEditor(1);
  EXPECT_EQ(ui.panel(), EditorAddRemove::Panel::Selected);
  EXPECT_EQ(lib.writes, 0u);
  ui.back();
  ui.chooseEditor(4);  // Reset.
  EXPECT_EQ(ui.panel(), EditorAddRemove::Panel::Reset);
  EXPECT_EQ(ui.focus(), 0);
  ui.activateEditor();  // Keep.
  EXPECT_EQ(lib.writes, 0u);
  ui.chooseEditor(4);
  ui.chooseEditor(1);  // Explicit reset.
  EXPECT_EQ(lib.writes, 1u);
  EXPECT_EQ(lib.captured.revision, 7u);
  EXPECT_EQ(lib.captured.kind, LayoutEditKind::Reset);
  ui.back();
  lib.result.status = LayoutEditStatus::Saved;
  EXPECT_FALSE(ui.pollEditor());  // Late completion cannot reopen editor.
  EXPECT_EQ(ui.panel(), EditorAddRemove::Panel::Closed);
}
TEST(EditorAddRemove, AddRemoveCompletionFocusAndStaleSelection) {
  EditorFixture lib;
  lib.page.revision = 1;
  lib.page.count = 3;
  EditorAddRemove ui(lib);
  ui.enter();
  ui.chooseEditor(3);
  ui.chooseEditor(2);
  EXPECT_EQ(lib.captured.kind, LayoutEditKind::Add);
  EXPECT_EQ(lib.captured.widget, WidgetKind::Browse);
  lib.page.count = 4;
  lib.page.revision = 2;
  lib.result.status = LayoutEditStatus::Saved;
  EXPECT_TRUE(ui.pollEditor());
  EXPECT_LT(ui.focus(), ui.rows());
  ui.chooseEditor(3);
  ui.chooseEditor(0);
  EXPECT_EQ(lib.captured.kind, LayoutEditKind::Remove);
  EXPECT_EQ(lib.captured.index, 3);
  lib.page.count = 3;
  lib.page.revision = 3;
  lib.result.status = LayoutEditStatus::Saved;
  EXPECT_TRUE(ui.pollEditor());
  EXPECT_LT(ui.focus(), ui.rows());
  lib.page.revision = 4;
  ui.chooseEditor(0);
  EXPECT_EQ(ui.panel(), EditorAddRemove::Panel::Error);
  EXPECT_EQ(ui.editorFailure(), LayoutEditStatus::Stale);
  EXPECT_EQ(lib.writes, 2u);
}
TEST(EditorAddRemove, X3PresentationIsNotEnabledWithoutApproval) {
  EditorFixture lib;
  lib.page.revision = 1;
  lib.page.textOnly = true;
  EditorAddRemove ui(lib);
  ui.enter();
  EXPECT_EQ(ui.panel(), EditorAddRemove::Panel::Closed);
  EXPECT_EQ(lib.writes, 0u);
}
TEST(LayoutEditor, RemovesFirstMiddleLastAndAllWithoutChangingOtherBytes) {
  for (uint8_t index : {0, 1, 2}) {
    auto raw = original();
    LayoutEditor editor;
    auto edit = request(LayoutEditKind::Remove);
    edit.index = index;
    ASSERT_EQ(editor.prepare(LayoutInput::memory(raw.data(), raw.size()), "x4", edit), LayoutEditStatus::Saved);
    raw = bytes(editor.output());
    Layout layout;
    HomePage page;
    ASSERT_TRUE(layout.project(LayoutInput::memory(raw.data(), raw.size()), "x4", 0, page));
    EXPECT_EQ(page.count, 2);
    EXPECT_EQ(page.widgets[0].kind, index == 0 ? WidgetKind::Shelf : WidgetKind::Continue);
    EXPECT_NE(raw.find("\"overrides\""), std::string::npos);
  }
  auto raw = original();
  LayoutEditor editor;
  for (unsigned revision = 1; revision < 4; ++revision) {
    auto edit = request(LayoutEditKind::Remove, revision);
    ASSERT_EQ(editor.prepare(LayoutInput::memory(raw.data(), raw.size()), "x4", edit), LayoutEditStatus::Saved);
    raw = bytes(editor.output());
  }
  Layout layout;
  HomePage page;
  ASSERT_TRUE(layout.project(LayoutInput::memory(raw.data(), raw.size()), "x4", 0, page));
  EXPECT_EQ(page.count, 0);
  ASSERT_EQ(editor.prepare(LayoutInput::memory(raw.data(), raw.size()), "x4", request(LayoutEditKind::Add, 4)),
            LayoutEditStatus::Saved);
}
TEST(LayoutEditor, MissingOverrideGroupsAndGeneratedIdCollision) {
  auto raw = original();
  const auto start = raw.find("\"overrides\":");
  raw.replace(start, raw.size() - start, "\"overrides\":{}} ");
  // Without class reflow the 4-column hero does not fit, so use a common 2-column base.
  raw.replace(raw.find("\"w\":4"), 5, "\"w\":2");
  raw.replace(raw.find("\"x\":2"), 5, "\"x\":0");
  raw.replace(raw.rfind("\"y\":3"), 5, "\"y\":5");
  LayoutEditor editor;
  ASSERT_EQ(editor.prepare(LayoutInput::memory(raw.data(), raw.size()), "x4", request(LayoutEditKind::Add)),
            LayoutEditStatus::Saved);
  auto changed = bytes(editor.output());
  EXPECT_NE(changed.find("\"x3\""), std::string::npos);
  EXPECT_NE(changed.find("\"x4\""), std::string::npos);
  raw = original();
  raw.insert(raw.find("\"x4\":{\"resume\"") + 6, "\"added-2-0\":{\"hidden\":true},");
  ASSERT_EQ(editor.prepare(LayoutInput::memory(raw.data(), raw.size()), "x4", request(LayoutEditKind::Add)),
            LayoutEditStatus::Saved);
  EXPECT_NE(bytes(editor.output()).find("\"instance\":\"added-2-1\""), std::string::npos);
}
TEST(LayoutEditor, BrokenSourceReadAndRevisionExhaustionCannotPublish) {
  LayoutEditor editor;
  auto raw = original();
  raw.replace(raw.find("\"revision\":1"), 12, "\"revision\":2147483647");
  EXPECT_EQ(
      editor.prepare(LayoutInput::memory(raw.data(), raw.size()), "x4", request(LayoutEditKind::Reset, 2147483647)),
      LayoutEditStatus::Invalid);
  LayoutInput broken{[](void*, size_t, void*, size_t) { return false; }, &editor, 100};
  EXPECT_EQ(editor.prepare(broken, "x4", request(LayoutEditKind::Remove)), LayoutEditStatus::Invalid);
  EXPECT_EQ(editor.output().bytes, 0u);
}
TEST(LayoutEditor, UnknownInstancesReserveCellsAndSurviveKnownRemoval) {
  auto raw = original();
  const std::string unknown =
      R"({"instance":"future","widget":"future-widget","version":99,"x":2,"y":6,"w":2,"h":1,"settings":{"opaque":[null,{"keep":"exact"}]}})";
  raw.insert(raw.find("\"placements\":[") + 14, unknown + ",");
  for (const char* device : {"x4", "x3"}) {
    const auto key = std::string("\"") + device + "\":{\"resume\"";
    raw.insert(raw.find(key) + 6, "\"future\":{\"x\":0,\"y\":5},");
  }
  LayoutEditor editor;
  ASSERT_EQ(editor.prepare(LayoutInput::memory(raw.data(), raw.size()), "x4", request(LayoutEditKind::Add)),
            LayoutEditStatus::Saved);
  raw = bytes(editor.output());
  EXPECT_NE(raw.find(unknown), std::string::npos);
  Layout parser;
  HomePage page;
  ASSERT_TRUE(parser.project(LayoutInput::memory(raw.data(), raw.size()), "x4", 0, page));
  ASSERT_EQ(page.count, 4);
  EXPECT_EQ(page.widgets[3].y, 6);  // The unknown tile reserved row 5.
  auto edit = request(LayoutEditKind::Remove, 2);
  ASSERT_EQ(editor.prepare(LayoutInput::memory(raw.data(), raw.size()), "x4", edit), LayoutEditStatus::Saved);
  EXPECT_NE(bytes(editor.output()).find(unknown), std::string::npos);
}
