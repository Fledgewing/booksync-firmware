// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "HistorySource.h"
#include "booksync/storage/Index.h"
#include "widgets/BookList.h"
namespace booksync {
struct PersonalCard;
enum class Invalidation { Files, Capture, Generation, Account };
// Worker materializes one visible window; renderer gets only a copied value.
// Caller excludes rendering during mutation (the existing RenderLock contract).
class ReadModel {
 public:
  explicit ReadModel(storage::Index& index) : index_(index), lifetime_(++nextLifetime_) {}
  bool materialize(WindowRequest request);
  void setContinue(const BookRow& row, bool available, const PersonalCard* resolved = nullptr);
  void copyWindow(LibraryWindow& out) const { out = window_; }
  void setBookListScratch(BookListProjection* scratch) { bookListScratch_ = scratch; }
  // Borrowed frozen selection, retired before its owner is released. Only the
  // normalized source owner supplies membership/order/year and saved age.
  void setBookListSource(BookListSource source, ReadingScope scope, uint16_t year) {
    bookListSource_ = source;
    bookListScope_ = scope;
    bookListYear_ = year;
  }
  bool materializeBatch(const WindowBatchRequest& batch);
  static bool accepts(const WindowBatchRequest& batch);
  Availability availability() const { return window_.availability; }
  LibraryWindow window() const { return window_; }
  void invalidate(Invalidation reason);
  // Query snapshots share the Library subscription, without making book windows stale.
  void notifyQueryPublication() { publishChanged(); }
  bool beginHistory(HistorySource source);
  bool historyTick(uint16_t allowance = HistoryBatch);
  bool historyBusy() const { return historyBusy_; }
  uint16_t lastHistoryWork() const { return lastHistoryWork_; }
  static constexpr uint16_t HistoryBatch = 256;
  Subscription subscribe() const { return {sequence_, lifetime_, true}; }
  bool changed(Subscription& value) const {
    if (!value.live || value.lifetime != lifetime_ || value.sequence == sequence_) return false;
    value.sequence = sequence_;
    return true;
  }
  void retire(Subscription& value) const { value.live = false; }

 private:
  storage::Index& index_;
  LibraryWindow window_{};
  BookRow continue_{};
  BookListProjection* bookListScratch_ = nullptr;  // Optional X4 worker lifetime, owned by facade.
  BookListSource bookListSource_{};
  ReadingScope bookListScope_{};
  uint16_t bookListYear_ = 0;
  bool materializeList(WindowSpan&);
  bool continueAvailable_ = false, continueProgressResolved_ = false;
  inline static uint64_t nextPublication_ = 0;
  void publishChanged() {
    sequence_ = ++nextPublication_;
    window_.publication = sequence_;
  }
  inline static uint64_t nextLifetime_ = 0;  // Main-loop serialized; never reset with Home.
  // Zero means no publication; the first global publication must notify subscribers.
  uint64_t sequence_ = 0, lifetime_ = 0;
  struct ReadingValue {
    uint64_t hash = 0, revision = 0;
    uint32_t page = 0, pages = 0, seconds = 0;
    bool known = false, statsKnown = false;
  };
  std::array<ReadingValue, LibraryRows> reading_{};
  storage::IndexRecord record_{};  // Reused path scratch; never resident catalog/history.
  HistorySource history_{};
  ReadingVisit visit_{};  // Reused cursor item keeps worker task frames small.
  Provenance historyProvenance_{};
  uint64_t historyRevision_ = 0;
  uint16_t lastHistoryWork_ = 0;
  bool historyBusy_ = false, historyEnded_ = false;
};
}  // namespace booksync
