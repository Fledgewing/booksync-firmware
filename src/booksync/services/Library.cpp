// SPDX-License-Identifier: GPL-3.0-or-later
#include <Logging.h>
#include <Memory.h>

#include "LocalLibrary.h"
#include "NativeHistory.h"
#include "OfflineLibrary.h"
#include "SdIndexSource.h"
#include "booksync/storage/LayoutStore.h"
namespace booksync {
namespace {
struct LocalState {
  storage::Index index{{"/.booksync/device.index", "/.booksync/device.index.tmp", "/.booksync/device.index.bak"},
                       "/.booksync/device.a",
                       "/.booksync/device.b"};
  storage::LayoutStore layout{
      {"/.booksync/home-layout.json", "/.booksync/home-layout.json.tmp", "/.booksync/home-layout.json.bak"},
      Layout::deviceClass()};
  bool layoutPending = true;
  ReadModel model{index};
  DeviceIndex worker{index, model};
  SdIndexSource source;
  NativeHistory history;
  WindowBatchRequest pending{};
  storage::IndexRecord openRecord{};
  BookKey openKey{};
  bool requested = true, rescan = false, resolveOpen = false, openReady = false;
};
static_assert(sizeof(LocalState) <= 24 * 1024);
// One fallible allocation per Home lifetime, <=24 KiB. These codec/window/history
// buffers exceed the task stack; releasing them before reader entry keeps C3
// buffers out of the reader lifetime. HAL owns its bounded directory-handle allocations.
std::unique_ptr<LocalState> local;
class OfflineLibrary final : public Library {
 public:
  bool available = false;
  bool openRequested = false;
  Capabilities capabilities() const override { return {true, true, false}; }
  Snapshot snapshot() const override {
    Snapshot value;
    value.canOpenLastBook = available;
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
  CommandStatus open(BookKey key) override {
    if (!local || !local->index.ready() || key.provenance != local->index.provenance() ||
        key.ordinal >= local->index.count())
      return CommandStatus::Unavailable;
    local->openKey = key;
    local->resolveOpen = true;
    local->openReady = false;
    return CommandStatus::Accepted;
  }
  LibraryWindow window() const override { return local ? local->model.window() : LibraryWindow{}; }
  Subscription subscribe() const override { return local ? local->model.subscribe() : Subscription{}; }
  bool changed(Subscription value) const override { return local && local->model.changed(value); }
  CommandStatus submit(Intent intent) override {
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
void publishHomeBook(bool available) {
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
  local->pending.windows[0] = {};
  local->pending.count = 1;
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
  if (!local) return;
  auto& state = *local;
  if (state.layoutPending) {
    state.layoutPending = false;
    state.layout.load();  // Read-only recovery before F34 consumes a Library projection.
  }
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
    state.requested = true;
  }
  if (state.requested) {
    state.requested = false;
    if (state.model.materializeBatch(state.pending) && state.model.availability() == Availability::Ready &&
        state.history.start())
      state.model.beginHistory(state.history.source());
  }
  if (state.model.historyBusy()) state.model.historyTick(DeviceIndex::Batch);
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
    local->requested = true;
  }
}
}  // namespace booksync
