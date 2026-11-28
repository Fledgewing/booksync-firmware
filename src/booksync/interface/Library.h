#pragma once

#include "Capabilities.h"
#include "Commands.h"
#include "Models.h"
#include "booksync/interface/AccountForm.h"
#include "booksync/interface/AccountView.h"
#include "booksync/interface/CatalogView.h"
#include "booksync/interface/Clock.h"
#include "booksync/interface/Covers.h"
#include "booksync/interface/Device.h"
#include "booksync/interface/Downloads.h"
#include "booksync/interface/HomeLayout.h"
#include "booksync/interface/LayoutEdit.h"
#include "booksync/interface/Queries.h"
#include "booksync/interface/Reminders.h"
#include "booksync/interface/Sleep.h"
#include "booksync/interface/Series.h"
#include "booksync/interface/SeriesFlow.h"
#include "booksync/interface/StatsWidgets.h"
#include "booksync/interface/PersonalWidgets.h"
#include "booksync/interface/TimeWidgets.h"

namespace booksync {
// UI reads bounded published values. No scans, writes, parsing or transport in
// snapshot(). submit() reports acceptance only; it never proves remote delivery.
class Library {
 public:
  virtual ~Library() = default;
  virtual Capabilities capabilities() const = 0;
  virtual void remindersList(ReminderList& out) const { out.clear(); }
  virtual ReminderResult remindersMutate(const ReminderMutation&) { return {}; }
  virtual bool reminderSleepAvailable() const { return false; }
  virtual bool remindersRetry() { return false; }
  virtual ReminderResult remindersResult() const { return {}; }
  virtual ReminderEvents remindersEvents() const { return {}; }
  virtual bool remindersDue(ReminderDue& out) const {
    (void)out;
    return false;
  }
  virtual ReminderResult remindersClaim(const ReminderDue&, uint64_t) { return {}; }
  virtual ReminderResult remindersRespond(uint64_t, uint64_t, ReminderAction) { return {}; }
  virtual void remindersRetire(uint64_t) {}
  virtual ReminderPresentation remindersPresentation(ReminderClaim) const { return {}; }
  virtual void remindersActivity(ReminderActivity) {}
  virtual ReminderAuthorization remindersAuthorize(ReminderClaim) { return {}; }
  virtual Snapshot snapshot() const = 0;
  virtual SleepView sleepView() const { return {}; }
  virtual void readPersonalWidgets(PersonalWidgetBatch& out) const { out.clear(); }
  virtual ClockStatus clockStatus() const { return {}; }
  virtual bool requestDeviceStatus() { return false; }
  virtual DeviceSnapshot deviceStatus() const { return {}; }
  virtual DeviceResult deviceResult() const { return {}; }
  virtual CommandStatus deviceCommand(DeviceCommand) { return CommandStatus::Unavailable; }
  virtual void readTimeWidgets(TimeWidgetBatch& out) const { out = {}; }
  virtual AccountView accountView() const { return {}; }
  virtual CommandStatus accountAction(AccountFormRequest) { return CommandStatus::Unavailable; }
  virtual void readAccountForm(AccountFormView& out) const { out = {}; }
  virtual CommandStatus submitAccount(AccountFormRequest, AccountEntry& entry) {
    entry.clear();
    return CommandStatus::Unavailable;
  }
  virtual CommandStatus selectService(ServiceSelection) { return CommandStatus::Unavailable; }
  // Called outside paint; one pending visible window, repeated requests coalesce.
  virtual bool requestWindow(WindowRequest) { return false; }
  virtual bool requestBatch(WindowBatchRequest) { return false; }
  // Selection is a saved book key and manifest ordinal; path/identity/transport
  // resolution remains private. UI may poll cached values outside paint and
  // retire its F12 delivery when leaving the originating view.
  virtual CommandStatus download(BookKey, uint8_t) { return CommandStatus::Unavailable; }
  virtual DownloadStatus downloadStatus(uint64_t) const { return {}; }
  virtual CommandStatus resumeDownload(uint64_t) { return CommandStatus::Unavailable; }
  virtual CommandStatus cancelDownload(uint64_t) { return CommandStatus::Unavailable; }
  virtual bool requestQuery(const QueryRequest&) { return false; }
  virtual void retireQuery(uint64_t) {}
  virtual void readQuery(QueryResult& out) const {
    out.clear();
    out.availability = Availability::Unsupported;
  }
  // Worker-owned next-N selection. Unregistered providers fail closed; callers
  // never reconstruct selection by scanning their partial visible windows.
  virtual bool requestSeries(SeriesRequest) { return false; }
  virtual void readSeries(SeriesPlanView& out) const {
    out.clear();
    out.availability = Availability::Unsupported;
  }
  // Sole worker owns manifests, frozen choices, admission and queue execution.
  virtual CommandStatus seriesAction(const SeriesFlowRequest&) { return CommandStatus::Unavailable; }
  virtual void readSeriesFlow(SeriesFlowView& out) const { out.clear(); }
  virtual CommandStatus searchSeries(std::array<char, 64>) { return CommandStatus::Unavailable; }
  // Post-paint intent; cached reference/bitmap reads perform no filesystem or decode work.
  virtual CommandStatus requestCover(BookKey) { return CommandStatus::Unavailable; }
  virtual CoverReference coverReference(BookKey) const { return {}; }
  virtual bool readCover(CoverReference, CoverBitmap& out) const {
    out = {};
    return false;
  }
  virtual CommandStatus showBook(BookKey key) { return open(key); }
  // F36 cached presentation projections. Dispatch/request methods run outside paint.
  virtual CatalogSummary catalogSummary() const { return {}; }
  virtual bool requestCatalogSources(CatalogSourceRequest) { return false; }
  virtual void readCatalogSources(CatalogSources& out) const {
    out = {};
    out.availability = Availability::Unsupported;
  }
  virtual void readDownloadChoices(BookKey, DownloadChoices& out) const { out = {}; }
  virtual DownloadStatus bookDownloadStatus(BookKey) const { return {}; }
  virtual CommandStatus connectForDownload(BookKey) { return CommandStatus::Unavailable; }
  virtual CommandStatus showBookDetails(BookKey) { return CommandStatus::Unavailable; }
  virtual CommandStatus showCatalogSeries(std::array<char, 64>) { return CommandStatus::Unavailable; }
  virtual CommandStatus showCatalogFilter() { return CommandStatus::Unavailable; }
  virtual CommandStatus showDownloadAccount() { return submit(Intent::OpenAccount); }
  virtual CommandStatus open(BookKey) { return CommandStatus::Unavailable; }
  virtual LibraryWindow window() const { return {}; }
  virtual void readWindow(LibraryWindow& out) const { out = window(); }
  virtual void readStatsWidgets(StatsWidgetBatch& out) const {
    out.requested = (1u << StatsWidgetCount) - 1;
    for (unsigned i = 0; i < StatsWidgetCount; ++i) {
      out.cards[i].clear();
      out.cards[i].state = StatsCardState::Unsupported;
    }
  }
  virtual HomePage homePage() const { return {}; }
  virtual bool requestHomePage(uint8_t) { return false; }
  virtual CommandStatus editLayout(LayoutEdit) { return CommandStatus::Unavailable; }
  virtual LayoutPreview previewLayout(HomePage, uint8_t, LayoutStep) const { return {}; }
  virtual LayoutEditResult layoutEditResult() const { return {}; }
  virtual Subscription subscribe() const { return {}; }
  virtual bool changed(Subscription) const { return false; }

  virtual CommandStatus submit(Intent intent) = 0;
};
}  // namespace booksync
