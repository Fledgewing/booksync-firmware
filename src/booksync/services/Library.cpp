// SPDX-License-Identifier: GPL-3.0-or-later
#include <Logging.h>
#include <Memory.h>

#include "DeviceBridge.h"
#include "LayoutGeometry.h"
#include "LocalLibrary.h"
#include "NativeHistory.h"
#include "PersonalWorker.h"
#include "OfflineLibrary.h"
#include "Queries.h"
#include "RemindersFacade.h"
#include "SdIndexSource.h"
#include "booksync/storage/LayoutStore.h"
#include "widgets/StatsWorker.h"
#include "widgets/Clock.h"
#include "widgets/Moon.h"
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
  TimeWidgetBatch time{};
  int64_t timeMinute = -1;
  ClockEvidence timeEvidence = ClockEvidence::Unknown;
  int32_t timeOffset = 0;
  bool twelveHour = false;
  storage::IndexRecord openRecord{};
  BookKey openKey{};
  std::unique_ptr<LocalQueryState> query;
  std::unique_ptr<StatsWorker> stats;
  ServerAdapter* savedStatsSource = nullptr;  // Borrowed Home-lifetime provider; Bootstrap owns it.
  std::unique_ptr<PersonalWorker> personal;
  bool personalPending = false;
  bool queryChanged = false;
  uint64_t queryRequestId = 0;
  bool requested = false, rescan = false, resolveOpen = false, openReady = false;
};
static_assert(sizeof(LocalState) <= 28 * 1024);
// One fallible allocation per Home lifetime, <=28 KiB. These codec/window/history
// buffers and the <=4 KiB editor splice workspace exceed the task stack; releasing them before reader entry keeps C3
// buffers out of the reader lifetime. HAL owns its bounded directory-handle allocations.
std::unique_ptr<LocalState> local;
SleepView currentSleep{};
DeviceBridge* devices = nullptr;
bool deviceWanted = false;
RemindersFacade* reminders = nullptr;
void (*reminderConnectHook)() = nullptr;
void (*reminderActivityHook)(ReminderActivity) = nullptr;
ClockStatus currentClock{};
AccountView accountPresentation{};
// One fixed <=368-byte cached form plus an 8-byte monotonic counter: avoids
// a heap allocation for the offline capability view and never holds credentials.
AccountFormView formPresentation{};
uint64_t nextAccountForm = 0;
bool catalogRequested = false, catalogAccountRequested = false;
bool noticeDismissalPending = false;
// Presentation retirement spans Home instances, independently of SD checkpoint
// success. A failed write must not reopen a dismissed notice during this boot.
bool noticePresentationRetired = false;
bool noticePending = false, upgradeRecovery = false;
bool (*noticeCheckpoint)() = nullptr;
class OfflineLibrary final : public Library {
 public:
  bool available = false;
  bool openRequested = false;
  BookRow lastBook{};
  SleepView sleepView() const override { return currentSleep; }
  ClockStatus clockStatus() const override { return currentClock; }
  void readPersonalWidgets(PersonalWidgetBatch& out) const override {
    if (local && local->personal) out = local->personal->result(); else out.clear();
  }
  Capabilities capabilities() const override {
    ReminderCapabilities support{reminders != nullptr};
    support.sleep = reminders && reminders->sleepAvailable();
    support.clockKnown = currentClock.known();
    return {true, true, false, support};
  }
  void readTimeWidgets(TimeWidgetBatch& out) const override { out = local ? local->time : TimeWidgetBatch{}; }
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
  CommandStatus accountAction(AccountFormRequest request) override {
    if (request.ticket.owner != accountPresentation.ticket.owner ||
        request.ticket.epoch != accountPresentation.ticket.epoch)
      return CommandStatus::Unavailable;
    if (request.action == AccountAction::Begin) {
      // The default adapter currently supports retained connectors only. This
      // local facade must never imply a live account/phone transport exists.
      if (accountPresentation.accountSupported || nextAccountForm == UINT64_MAX) return CommandStatus::Unavailable;
      formPresentation = {};
      formPresentation.account = accountPresentation;
      formPresentation.origin = request.ticket;
      formPresentation.form = ++nextAccountForm;
      formPresentation.revision = 1;
      formPresentation.scene = AccountScene::Limited;
      return CommandStatus::Accepted;
    }
    if (request.form == formPresentation.form && request.action == AccountAction::Cancel) {
      formPresentation = {};
      return CommandStatus::Accepted;
    }
    return CommandStatus::Unsupported;
  }
  void readAccountForm(AccountFormView& out) const override { out = formPresentation; }
  void remindersList(ReminderList& out) const override {
    if (reminders)
      reminders->list(out);
    else
      out.clear();
  }
  ReminderResult remindersMutate(const ReminderMutation& m) override {
    return reminders ? reminders->mutate(m) : ReminderResult{};
  }
  void remindersActivity(ReminderActivity activityValue) override {
    if (reminderActivityHook) reminderActivityHook(activityValue);
  }
  bool reminderSleepAvailable() const override { return reminders && reminders->sleepAvailable(); }
  bool remindersRetry() override { return reminders && reminders->retry(); }
  ReminderResult remindersResult() const override { return reminders ? reminders->result() : ReminderResult{}; }
  ReminderEvents remindersEvents() const override { return reminders ? reminders->events() : ReminderEvents{}; }
  bool remindersDue(ReminderDue& out) const override { return reminders && reminders->due(out); }
  ReminderResult remindersClaim(const ReminderDue& d, uint64_t owner) override {
    return reminders ? reminders->claim(d, owner) : ReminderResult{};
  }
  ReminderResult remindersRespond(uint64_t token, uint64_t owner, ReminderAction action) override {
    return reminders ? reminders->respond(token, owner, action) : ReminderResult{};
  }
  ReminderPresentation remindersPresentation(ReminderClaim claim) const override {
    return reminders ? reminders->presentation(claim) : ReminderPresentation{};
  }
  ReminderAuthorization remindersAuthorize(ReminderClaim claim) override {
    return reminders ? reminders->authorize(claim) : ReminderAuthorization{};
  }
  void remindersRetire(uint64_t owner) override {
    if (reminders) reminders->retire(owner);
  }
  Snapshot snapshot() const override {
    Snapshot value;
    value.canOpenLastBook = available;
    value.upgradeNoticePending = noticePending && !noticePresentationRetired;
    value.upgradeRecoveryRequired = upgradeRecovery;
    if (local) {
      value.catalog = local->model.availability();
      value.generation = local->index.provenance().generation;
      value.accountEpoch = local->index.provenance().accountEpoch;
    }
    return value;
  }
  CatalogSummary catalogSummary() const override {
    CatalogSummary value;
    if (local) {
      value.provenance = local->index.provenance();
      value.publication = local->model.subscribe().sequence;
      // A device index proves local files only; never impersonate server totals.
      value.onCard = {local->index.count(), local->index.ready() ? Availability::Ready : Availability::NotSaved};
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
    if (std::string_view(Layout::deviceClass()) == "x3" && batch.personalWidgets) return false;
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
    if (accepted) {
      local->queryChanged = true;
      local->queryRequestId = request.requestId;
    }
    return accepted;
  }
  void retireQuery(uint64_t requestId) override {
    if (!local || !local->query || local->queryRequestId != requestId) return;
    local->query->worker.cancel();
    local->query.reset();  // Release the optional F22 workspace when its foreground owner leaves.
    local->queryChanged = true;
  }
  void readQuery(QueryResult& out) const override {
    if (local && local->query)
      local->query->worker.copyResult(out);
    else {
      out.clear();
    }
  }
  void readStatsWidgets(StatsWidgetBatch& out) const override {
    if (local && local->stats && accountPresentation.ticket.epoch <= UINT32_MAX) {
      out = local->stats->published();
      return;
    }
    out.requested = local ? local->pending.statsWidgets : 0;
    for (auto& statsValue : out.cards) {
      statsValue.clear();
      statsValue.state = StatsCardState::Unavailable;
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
  bool requestDeviceStatus() override {
    deviceWanted = devices != nullptr;
    return deviceWanted;
  }
  DeviceSnapshot deviceStatus() const override { return devices ? devices->snapshot() : DeviceSnapshot{}; }
  DeviceResult deviceResult() const override { return devices ? devices->result() : DeviceResult{}; }
  CommandStatus deviceCommand(DeviceCommand command) override {
    return devices ? devices->submit(command) : CommandStatus::Unavailable;
  }
  CommandStatus submit(Intent intent) override {
    if (intent == Intent::OpenAccount) {
      catalogAccountRequested = true;
      return CommandStatus::Accepted;
    }
    if (intent == Intent::BrowseLibrary) {
      catalogRequested = true;
      return CommandStatus::Accepted;
    }
    if (intent == Intent::ConnectReminderTime) {
      if (!reminderConnectHook) return CommandStatus::Unavailable;
      reminderConnectHook();
      return CommandStatus::Accepted;
    }
    if (intent == Intent::DismissUpgradeNotice) {
      if (!noticeCheckpoint) return CommandStatus::Unavailable;
      noticePresentationRetired = true;
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
void publishSleepView(SleepView value) { currentSleep = value; }
void bindReminderConnect(void (*callback)()) { reminderConnectHook = callback; }
void bindReminderActivity(void (*callback)(ReminderActivity)) { reminderActivityHook = callback; }
void publishClockStatus(ClockStatus value) {
  const bool changedDay = currentClock.hasDateTime() != value.hasDateTime() ||
      currentClock.local.year != value.local.year || currentClock.local.month != value.local.month ||
      currentClock.local.day != value.local.day;
  currentClock = value;
  if (changedDay && local && local->pending.personalWidgets) {
    if (local->personal) local->personal->retire();
    local->personalPending = true;
    local->model.notifyQueryPublication();
  }
}
void bindReminders(RemindersFacade& facade) { reminders = &facade; }
void publishAccountView(AccountView value) {
  value.serviceLabel.back() = 0;
  const bool ownerChanged = value.ticket.owner != accountPresentation.ticket.owner ||
      value.ticket.epoch != accountPresentation.ticket.epoch || value.selectedService != accountPresentation.selectedService;
  accountPresentation = value;
  if (ownerChanged && local && local->pending.personalWidgets) {
    if (local->personal) local->personal->retire();
    local->personalPending = true;
    local->model.notifyQueryPublication();
  }
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
bool takeCatalogAccountRequest() {
  const bool requested = catalogAccountRequested;
  catalogAccountRequested = false;
  return requested;
}
bool takeCatalogRequest() {
  const bool requested = catalogRequested;
  catalogRequested = false;
  return requested;
}
bool takeHomeOpenRequest() {
  const bool requested = shellLibrary().openRequested;
  shellLibrary().openRequested = false;
  return requested;
}
bool localLibraryActive() { return bool(local); }
void enterLocalLibrary(ServerAdapter* savedStatsSource) {
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
  local->savedStatsSource = savedStatsSource;
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
void bindDeviceBridge(DeviceBridge* bridge) {
  devices = bridge;
  deviceWanted = false;
}
bool deviceStatusWanted() { return deviceWanted; }
void notifyDevicePublication() {
  if (local) local->model.notifyQueryPublication();
}
void leaveLocalLibrary() {
  if (!local) return;
  local->worker.cancel();
  local->source.close();
  local.reset();
}
void workTimeWidgets(bool twelveHour) {
  if (!local) return;
  auto& state = *local;
  if (state.time.requested &&
      (state.timeMinute != currentClock.utcSeconds / 60 || state.timeEvidence != currentClock.evidence ||
       state.timeOffset != currentClock.utcOffsetSeconds || state.twelveHour != twelveHour)) {
    state.timeMinute = currentClock.utcSeconds / 60;
    state.timeEvidence = currentClock.evidence;
    state.timeOffset = currentClock.utcOffsetSeconds;
    state.twelveHour = twelveHour;
    if (state.time.requested & timeWidgetBit(TimeWidget::Clock))
      projectClockWidget(currentClock, {TimeCardState::Ready, 0, true, false, twelveHour}, state.time.clock);
    if (state.time.requested & timeWidgetBit(TimeWidget::Moon)) projectMoonWidget(currentClock, {}, state.time.moon);
    state.model.notifyQueryPublication();
  }
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
    if (state.editResult.status == LayoutEditStatus::Saved) {
      if (state.edit.kind == LayoutEditKind::CreatePage) state.wantedPage = state.edit.page + 1;
      if (state.edit.kind == LayoutEditKind::MovePage) state.wantedPage = state.edit.index;
    }
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
    if (!state.openReady) {
      LOG_ERR("BSI", "Indexed file unavailable; scheduling bounded device-index refresh");
      state.model.invalidate(Invalidation::Files);
      state.rescan = true;
      if (state.query) {
        state.query->worker.invalidate();
        state.queryChanged = true;
      }
    }
  }
  const bool scanning = state.worker.busy();
  if (scanning && !state.worker.tick()) state.source.close();
  if (scanning && !state.worker.busy()) {
    state.source.close();
    state.requested = state.pending.count != 0 || state.pending.personalWidgets;
  }
  if (state.requested) {
    state.requested = false;
    if (state.pending.statsWidgets && !state.page.textOnly && accountPresentation.ticket.epoch <= UINT32_MAX) {
      // <=30 KiB allocated once per Home lifetime, never on paint or C3. Fixed
      // F39 calendar/accumulator buffers cannot fit on the main-loop stack.
      if (!state.stats) {
        state.stats = makeUniqueNoThrow<StatsWorker>();
        if (state.stats) LOG_INF("BSW", "Stats workspace bounded allocation: %u bytes", unsigned(sizeof(StatsWorker)));
      }
      if (!state.stats)
        LOG_ERR("BSW", "Cannot allocate stats workspace (%u bytes)", unsigned(sizeof(StatsWorker)));
      else {
        auto provenance = state.index.provenance();
        provenance.owner = accountPresentation.ticket.owner;
        provenance.accountEpoch = uint32_t(accountPresentation.ticket.epoch);
        state.stats->begin(state.pending.statsWidgets, provenance, currentClock, state.savedStatsSource);
      }
    }
    if (!state.pending.statsWidgets && state.stats) state.stats->retire();
    state.personalPending = state.pending.personalWidgets != 0;
    if (!state.personalPending) state.personal.reset();
    state.time.requested = state.page.textOnly ? 0 : state.pending.timeWidgets;
    state.timeMinute = -1;
    state.model.setContinue(shellLibrary().lastBook, shellLibrary().available);
    if (state.model.materializeBatch(state.pending) && state.model.availability() == Availability::Ready &&
        state.history.start())
      state.model.beginHistory(state.history.source());
  }
  if (state.stats && state.stats->tick(currentClock)) state.model.notifyQueryPublication();
  if (state.model.historyBusy()) state.model.historyTick(DeviceIndex::Batch);
  if (!state.worker.busy() && state.personalPending) {
    state.personalPending = false;
    if (!state.personal) {
      // Optional requested-Home workspace <=16 KiB plus source's bounded 80 KiB
      // index. Both exceed the task stack and are released before reader entry.
      state.personal = makeUniqueNoThrow<PersonalWorker>(state.index);
      if (!state.personal) LOG_ERR("BSP", "Cannot allocate personal worker");
    }
    if (state.personal) state.personal->begin(state.pending.personalWidgets, state.pending.personalPick, currentClock);
    state.model.notifyQueryPublication();
  }
  if (!state.worker.busy() && state.personal && state.personal->busy()) {
    state.personal->tick();
    if (!state.personal->busy()) state.model.notifyQueryPublication();
  }
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
    if (local->personal) local->personal->retire();
    local->requested = local->pending.count != 0 || local->pending.personalWidgets;
  }
}
}  // namespace booksync
