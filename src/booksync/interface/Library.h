#pragma once

#include "Capabilities.h"
#include "Commands.h"
#include "Models.h"
#include "booksync/interface/HomeLayout.h"
#include "booksync/interface/LayoutEdit.h"

namespace booksync {
// UI reads bounded published values. No scans, writes, parsing or transport in
// snapshot(). submit() reports acceptance only; it never proves remote delivery.
class Library {
 public:
  virtual ~Library() = default;
  virtual Capabilities capabilities() const = 0;
  virtual Snapshot snapshot() const = 0;
  // Called outside paint; one pending visible window, repeated requests coalesce.
  virtual bool requestWindow(WindowRequest) { return false; }
  virtual bool requestBatch(WindowBatchRequest) { return false; }
  virtual CommandStatus open(BookKey) { return CommandStatus::Unavailable; }
  virtual LibraryWindow window() const { return {}; }
  virtual void readWindow(LibraryWindow& out) const { out = window(); }
  virtual HomePage homePage() const { return {}; }
  virtual bool requestHomePage(uint8_t) { return false; }
  virtual CommandStatus editLayout(LayoutEdit) { return CommandStatus::Unavailable; }
  virtual LayoutEditResult layoutEditResult() const { return {}; }
  virtual Subscription subscribe() const { return {}; }
  virtual bool changed(Subscription) const { return false; }

  virtual CommandStatus submit(Intent intent) = 0;
};
}  // namespace booksync
