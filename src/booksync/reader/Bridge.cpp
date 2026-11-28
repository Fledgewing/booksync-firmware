// SPDX-License-Identifier: GPL-3.0-or-later
#include "Bridge.h"

#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <optional>

#include "activities/RenderLock.h"
#include "activities/reader/NativeReaderObserver.h"
#include "activities/reader/ReadingSessionJournal.h"
namespace booksync::reader {
namespace {
constexpr storage::PublishPaths Paths{"/.booksync/reader.journal", "/.booksync/reader.journal.tmp",
                                      "/.booksync/reader.journal.bak"};
storage::LifecycleJournal journal(Paths);
storage::PageJournal pageHistory("/.booksync/pages");
PageCaptureQueue pageQueue;  // Pending accepted intervals must outlive reader activities.
bool pagesReady = false;
bool ready = false;
bool prepare() {
  if (ready) return true;
  if (!Storage.exists("/.booksync") && !Storage.mkdir("/.booksync")) {
    LOG_ERR("BSC", "Cannot create local capture directory");
    return false;
  }
  native_reader_observer::artifact = [](std::string_view kind, std::string_view book, const char* path, bool deleted) {
    std::optional<RenderLock> lock;
    if (!RenderLock::heldByCurrentTask()) lock.emplace();
    const auto result = Bridge::artifact(kind, book, path, deleted);
    if (result != storage::JournalResult::Ok) LOG_ERR("BSC", "Native artifact not captured: %u", unsigned(result));
  };
  ready = journal.recover() == storage::JournalResult::Ok;
  return ready;
}

}  // namespace
Bridge::Bridge() : capture_(journal), pages_(pageQueue) {}
void Bridge::open(std::string_view path, uint16_t format) {
  if (capture_.id() || !prepare()) return;
  const auto result = capture_.open(path, format);
  if (result != storage::JournalResult::Ok) LOG_ERR("BSC", "Local book capture unavailable: %u", unsigned(result));
}
namespace {
uint64_t pageEpoch(const ReadingStatsDateTime& local, uint8_t offset) {
  if (!local.isValid() || offset > 104) return 0;
  const int64_t epoch = 946684800LL + int64_t(readingStatsDayIndex(local.date)) * 86400 + local.hour * 3600 +
                        local.minute * 60 + local.second - (int(offset) - 48) * 900;
  return epoch >= 946684800LL ? uint64_t(epoch) : 0;
}
}  // namespace
void Bridge::startPage(const ReadingStatsDateTime& local, uint8_t offset, uint32_t spine, uint32_t page, uint32_t pages,
                       uint32_t fraction, uint32_t scale) {
  pageOffset_ = offset;
  pages_.start({capture_.id(), pageEpoch(local, offset), 0, 0, spine, page, pages, 0, fraction, scale, 0});
}
void Bridge::acceptPage(const ReadingStatsDateTime& end, uint8_t offset, uint32_t seconds) {
  pages_.accept(offset == pageOffset_ ? pageEpoch(end, offset) : 0, seconds);
}
void Bridge::observeSession(const ReadingSessionJournal::Record& record) {
  if (!capture_.id()) return;
  // Native finalization may release its own stats lock before calling us.
  // Borrow the record only while paint and its deferred native interval are excluded.
  std::optional<RenderLock> lock;
  if (!RenderLock::heldByCurrentTask()) lock.emplace();
  SessionObservation observation;
  memcpy(observation.id.data(), record.id, 32);
  memcpy(observation.document.data(), record.document, 32);
  observation.startedAt = record.startedAt;
  observation.endedAt = record.endedAt;
  observation.seconds = record.seconds;
  observation.utcOffsetMinutes = record.utcOffsetMinutes;
  capture_.session(observation);
}
void Bridge::observe(Sample sample, const std::string& cache, bool closed, bool refreshNative) {
  // Most loops do no work. Read raw slots only when live page/stats change or
  // closing, after the caller has excluded paint with RenderLock.
  if (pages_.hasWork()) {
    if (!pagesReady) pagesReady = pageHistory.recover() == storage::JournalResult::Ok;
    if (pagesReady && pages_.flush(pageHistory) != storage::JournalResult::Ok)
      LOG_ERR("BSC", "Page history pending; native saves continue independently");
  }
  if (!capture_.id()) return;
  sample.openingTotalSeconds = capture_.lastSample().openingTotalSeconds;
  sample.openingCounter = capture_.lastSample().openingCounter;
  sample.rawProgress = capture_.lastSample().rawProgress;
  sample.primaryBytes = capture_.lastSample().primaryBytes;
  sample.backupBytes = capture_.lastSample().backupBytes;
  const bool captureClosed = closed && !pages_.hasWork();
  if (!refreshNative && capture_.unchanged(sample, captureClosed)) return;
  for (unsigned slot = 0; slot < 2; ++slot) {
    char path[128];
    const auto n = snprintf(path, sizeof(path), "%s/progress.bin%s", cache.c_str(), slot ? ".bak" : "");
    if (n < 0 || static_cast<size_t>(n) >= sizeof(path)) {
      LOG_ERR("BSC", "Native progress path exceeds capture bound");
      return;
    }
    auto file = Storage.open(path, O_RDONLY);
    uint8_t length = 0;
    if (file && file.fileSize() <= 20) {
      const auto n = file.fileSize();
      if (file.read(sample.rawProgress.data() + slot * 20, n) == static_cast<int>(n)) length = n;
    }
    file.close();
    if (slot)
      sample.backupBytes = length;
    else
      sample.primaryBytes = length;
  }
  capture_.update(sample, captureClosed);
}
storage::JournalResult Bridge::artifact(std::string_view kind, std::string_view book, const char* path, bool deleted) {
  if (!prepare()) return storage::JournalResult::RecoveryRequired;
  Capture capture(journal);
  return capture.artifact(kind, book, path, deleted);
}

}  // namespace booksync::reader
