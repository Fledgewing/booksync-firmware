// SPDX-License-Identifier: GPL-3.0-or-later
#include <Logging.h>
#include <Memory.h>

#include "LayoutGeometry.h"
#include "LocalLibrary.h"
#include "NativeHistory.h"
#include "OfflineLibrary.h"
#include "Queries.h"
#include "SdIndexSource.h"
#include "booksync/storage/LayoutStore.h"
namespace booksync {
namespace {
struct LocalQueryState {
  StoredQuerySource source;
  Queries worker;
  explicit LocalQueryState(storage::Index& index) : source(index), worker(source.source()) {}
};
static_assert(sizeof(LocalQueryState) <= 48 * 1024);
struct LocalState {
  storage::Index index{{"/.booksync/device.index", "/.booksync/device.index.tmp", "/.booksync/device.index.bak"},
                       "/.booksync/device.a",
                       "/.booksync/device.b"};
  storage::LayoutStore layout{
      {"/.booksync/home-layout.json", "/.booksync/home-layout.json.tmp", "/.booksync/home-layout.json.bak"},
      Layout::deviceClass()};
  LayoutEditor editor{};
  LayoutEdit edit{};
  LayoutEditResult editResult{};
  bool layoutPending = true;
  HomePage page{};
  uint8_t wantedPage = 0;
  ReadModel model{index};
  DeviceIndex worker{index, model};
  SdIndexSource source;
  NativeHistory history;
  WindowBatchRequest pending{};
  storage::IndexRecord openRecord{};
  BookKey openKey{};
  std::unique_ptr<LocalQueryState> query;
  bool queryChanged = false;
  bool requested = false, rescan = false, resolveOpen = false, openReady = false;
};
static_assert(sizeof(LocalState) <= 28 * 1024);
// One fallible allocation per Home lifetime, <=28 KiB. These codec/window/history
// buffers and the <=4 KiB editor splice workspace exceed the task stack; releasing them before reader entry keeps C3
// buffers out of the reader lifetime. HAL owns its bounded directory-handle allocations.
std::unique_ptr<LocalState> local;
AccountView accountPresentation{};
bool noticeDismissalPending = false;
bool noticePending = false, upgradeRecovery = false;
bool (*noticeCheckpoint)() = nullptr;
class OfflineLibrary final : public Library {
 public:
  bool available = false;
  bool openRequested = false;
  BookRow lastBook{};
  Capabilities capabilities() const override { return {true, true, false}; }
  AccountView accountView() const override { return accountPresentation; }
  CommandStatus selectService(ServiceSelection selection) override {
    const auto& current = accountPresentation;
    if (selection.ticket.owner != current.ticket.owner || selection.ticket.epoch != current.ticket.epoch)
      return CommandStatus::Unavailable;
    if (selection.service && (!current.available || selection.service != current.service))
      return CommandStatus::Unsupported;
    accountPresentation.selectedService = selection.service;
    return CommandStatus::Accepted;
  }
  Snapshot snapshot() const override {
    Snapshot value;
    value.canOpenLastBook = available;
    value.upgradeNoticePending = noticePending;
    value.upgradeRecoveryRequired = upgradeRecovery;
    if (local) {
      value.catalog = local->model.availability();
      value.generation = local->index.provenance().generation;
      value.accountEpoch = local->index.provenance().accountEpoch;
    }
    return value;
  }
  bool requestWindow(WindowRequest request) override {
    WindowBatchRequest batch;
    batch.windows[0] = request;
    batch.bytes = request.bytes;
    batch.count = 1;
    return requestBatch(batch);
  }
  bool requestBatch(WindowBatchRequest batch) override {
    if (!local || !ReadModel::accepts(batch)) return false;
    local->pending = batch;
    local->requested = true;
    return true;
  }
  bool requestQuery(const QueryRequest& request) override {
    if (!local) return false;
    if (!local->query) {
      // Optional query-lifetime workspace <=48 KiB, too large for the worker
      // stack. No allocation on ordinary Home entry or per scanned record.
      local->query = makeUniqueNoThrow<LocalQueryState>(local->index);
      if (!local->query) {
        LOG_ERR("BSQ", "Cannot allocate query workspace (%u bytes)", unsigned(sizeof(LocalQueryState)));
        return false;
      }
    }
    const bool accepted = local->query->worker.start(request);
    if (accepted) local->queryChanged = true;
    return accepted;
  }
  void readQuery(QueryResult& out) const override {
    if (local && local->query)
      local->query->worker.copyResult(out);
    else {
      out.clear();
    }
  }
  CommandStatus open(BookKey key) override {
    if (!local || !local->index.ready() || key.provenance != local->index.provenance() ||
        key.ordinal >= local->index.count())
      return CommandStatus::Unavailable;
    local->openKey = key;
    local->resolveOpen = true;
    local->openReady = false;
    return CommandStatus::Accepted;
  }
  HomePage homePage() const override { return local ? local->page : HomePage{}; }
  bool requestHomePage(uint8_t page) override {
    if (!local) return false;
    local->wantedPage = page;
    local->layoutPending = true;
    return true;
  }
  CommandStatus editLayout(LayoutEdit edit) override {
    if (!local || local->editResult.status == LayoutEditStatus::Pending) return CommandStatus::Unavailable;
    local->edit = edit;
    local->editResult = {LayoutEditStatus::Pending, edit.revision};
    return CommandStatus::Accepted;
  }
  LayoutPreview previewLayout(HomePage draft, uint8_t index, LayoutStep step) const override {
    if (!local || draft.revision != local->page.revision || draft.page != local->page.page)
      return {draft, LayoutEditStatus::Stale};
    return previewGeometry(draft, index, step);
  }
  LayoutEditResult layoutEditResult() const override { return local ? local->editResult : LayoutEditResult{}; }
  void readWindow(LibraryWindow& out) const override {
    if (local)
      local->model.copyWindow(out);
    else
      out = {};
  }
  LibraryWindow window() const override { return local ? local->model.window() : LibraryWindow{}; }
  Subscription subscribe() const override { return local ? local->model.subscribe() : Subscription{}; }
  bool changed(Subscription value) const override { return local && local->model.changed(value); }
  CommandStatus submit(Intent intent) override {
    if (intent == Intent::DismissUpgradeNotice) {
      if (!noticeCheckpoint) return CommandStatus::Unavailable;
      noticeDismissalPending = true;
      return CommandStatus::Accepted;
    }
    if (intent == Intent::Refresh && local) {
      if (!local->worker.busy()) local->rescan = true;
      return CommandStatus::Accepted;
    }
    if (intent != Intent::OpenLastBook || !available) return CommandStatus::Unavailable;
    openRequested = true;
    return CommandStatus::Accepted;
  }
};
OfflineLibrary& shellLibrary() {
  static OfflineLibrary library;
  return library;
}
}  // namespace
Library& offlineLibrary() { return shellLibrary(); }
void publishAccountView(AccountView value) {
  value.serviceLabel.back() = 0;
  accountPresentation = value;
}
void publishUpgradeState(bool pending, bool recovery, bool (*dismiss)()) {
  noticePending = pending;
  upgradeRecovery = recovery;
  noticeCheckpoint = dismiss;
}
void publishHomeBook(bool available, const char* title) {
  shellLibrary().lastBook = {};
  if (title) {
    auto& text = shellLibrary().lastBook.title;
    size_t i = 0;
    while (i + 1 < text.size() && title[i]) {
      text[i] = title[i];
      ++i;
    }
  }
  shellLibrary().available = available;
  shellLibrary().openRequested = false;
}
bool takeHomeOpenRequest() {
  const bool requested = shellLibrary().openRequested;
  shellLibrary().openRequested = false;
  return requested;
}
void enterLocalLibrary() {
  reader::captureChanged = invalidateLocalCapture;
  if (local) return;
  if (!Storage.exists("/.booksync") && !Storage.mkdir("/.booksync")) {
    LOG_ERR("BSI", "Cannot create index directory");
    return;
  }
  local = makeUniqueNoThrow<LocalState>();
  if (!local) {
    LOG_ERR("BSI", "Cannot allocate local worker (%u bytes)", unsigned(sizeof(LocalState)));
    return;
  }
  if (!local->index.recover()) {
    local.reset();
    return;
  }
  LOG_INF("BSI", "Local worker bounded allocation: %u bytes", unsigned(sizeof(LocalState)));
  // Warm entry reads a saved manifest/window only. Cold scans yield to input
  // after sixteen entries and stop before any reader/storage handoff.
  if (!local->index.ready() && local->source.start() && !local->worker.start(local->source.source(), {1, 1, 0, 0}))
    local->source.close();
}
void leaveLocalLibrary() {
  if (!local) return;
  local->worker.cancel();
  local->source.close();
  local.reset();
}
void workLocalLibrary() {
  if (noticeDismissalPending) {
    noticeDismissalPending = false;
    if (!noticeCheckpoint || !noticeCheckpoint())
      LOG_ERR("BSA", "Notice dismissal checkpoint failed; retained for recovery");
  }
  if (!local) return;
  auto& state = *local;
  if (state.editResult.status == LayoutEditStatus::Pending) {
    state.editResult.status = state.layout.edit(state.editor, state.edit);
    state.wantedPage = state.edit.kind == LayoutEditKind::Reset && state.editResult.status == LayoutEditStatus::Saved
                           ? 0
                           : state.edit.page;
    state.layoutPending = true;
  }
  if (state.layoutPending) {
    state.layoutPending = false;
    state.layout.load();
    if (!state.layout.project(state.wantedPage, state.page)) {
      LOG_ERR("BSL", "Cannot project selected Home page; retaining source and using default");
      state.wantedPage = 0;
      state.layout.projectDefault(state.page);
    }
  }
  state.editResult.revision = state.page.revision;
  if (state.rescan) {
    state.rescan = false;
    auto provenance = state.index.provenance();
    ++provenance.generation;
    if (state.source.start() && !state.worker.start(state.source.source(), provenance)) state.source.close();
  }
  if (state.resolveOpen) {
    state.resolveOpen = false;
    state.openReady = state.index.read(state.openKey, state.openRecord) && Storage.exists(state.openRecord.path.data());
  }
  const bool scanning = state.worker.busy();
  if (scanning && !state.worker.tick()) state.source.close();
  if (scanning && !state.worker.busy()) {
    state.source.close();
    state.requested = state.pending.count != 0;
  }
  if (state.requested) {
    state.requested = false;
    state.model.setContinue(shellLibrary().lastBook, shellLibrary().available);
    if (state.model.materializeBatch(state.pending) && state.model.availability() == Availability::Ready &&
        state.history.start())
      state.model.beginHistory(state.history.source());
  }
  if (state.model.historyBusy()) state.model.historyTick(DeviceIndex::Batch);
  if (state.query && state.query->worker.busy()) {
    state.query->worker.tick();
    if (!state.query->worker.busy()) state.queryChanged = true;
  }
  if (state.queryChanged) {
    state.queryChanged = false;
    state.model.notifyQueryPublication();
  }
}
bool takeIndexedOpenRequest(std::string& path) {
  if (!local || !local->openReady) return false;
  local->openReady = false;
  if (local->openKey.provenance != local->index.provenance()) return false;
  // One user-initiated navigation string, copied into ActivityManager ownership.
  path = local->openRecord.path.data();
  return true;
}
void invalidateLocalCapture() {
  if (local) {
    local->model.invalidate(Invalidation::Capture);
    if (local->query) {
      local->query->worker.invalidate();
      local->queryChanged = true;
    }
    local->requested = local->pending.count != 0;
  }
}
}  // namespace booksync
