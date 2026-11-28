// SPDX-License-Identifier: GPL-3.0-or-later
// Inside the retained reader-friend harness. Main task only, using native reader
// operations and the same injected dwell clock used by the F03 baseline.
static bool readerCaptureScenarioTick() {
  const char* mode = std::getenv("BOOKSYNC_READER_CAPTURE_SCENARIO");
  if (!mode) return false;
  using namespace booksync::storage;
  using namespace booksync::reader;
  static unsigned phase = 0, ticks = 0;
  const auto finish = [](bool ok, const char* detail) {
    if (ok)
      LOG_INF("BSC", "BookSync reader capture scenario PASS: %s", detail);
    else
      LOG_ERR("BSC", "BookSync reader capture scenario FAIL: %s", detail);
    std::fflush(nullptr);
    std::_Exit(ok ? 0 : 1);
  };
  if (++ticks > 30000) finish(false, "reader did not settle");
  constexpr PublishPaths paths{"/.booksync/reader.journal", "/.booksync/reader.journal.tmp",
                               "/.booksync/reader.journal.bak"};
  if (phase == 0) {
    if (std::getenv("BOOKSYNC_SIZE_GATE") && !verifyBookSyncAdmissionHomeTick()) return true;
    if (strcmp(mode, "restart") == 0) {
      if (std::getenv("BOOKSYNC_SIZE_GATE")) {
        verifyBookSyncAdmissionReconnect();
        LOG_INF("F14", "BookSync F14 admission PASS: restart Home worker drained, display restored");
      }
      RenderLock lock;
      LifecycleJournal journal(paths);
      bool ok = journal.recover() == JournalResult::Ok;
      uint64_t id = 0;
      char bytes[2048];
      JournalEntry entry;
      Sample sample;
      bool closed = true, found = false;
      while (ok && journal.next("native", "local-unbound", id, id) == JournalResult::Ok) {
        ok = journal.read(id, entry, bytes, sizeof(bytes)) == JournalResult::Ok && entry.acknowledged == 0;
        if (entry.payload.substr(0, 4) == "BSO1" && Capture::decode(entry.checkpoint, sample, closed))
          found |= sample.sessionSeconds >= 10 && !closed && sample.hasSourceOffset;
      }
      finish(ok && found, "power cut retained unclosed native session and raw source observation");
    }
    SETTINGS.trackReadingStats = 1;
    SETTINGS.clockUtcOffsetQ = 48;
    activityManager.goToReader("/Books/Synthetic.epub", true);
    ++phase;
    return true;
  }
  if (phase == 2) {
    if (!activityManager.isHomeActivity()) return true;
    RenderLock lock;
    LifecycleJournal journal(paths);
    bool ok = journal.recover() == JournalResult::Ok;
    uint64_t id = 0;
    unsigned opens = 0, artifacts = 0, sessions = 0;
    uint64_t chapterOpen = 0;
    bool closed = false;
    char bytes[4096];
    JournalEntry entry;
    Sample sample;
    while (ok && journal.next("native", "local-unbound", id, id) == JournalResult::Ok) {
      ok = journal.read(id, entry, bytes, sizeof(bytes)) == JournalResult::Ok && entry.acknowledged == 0;
      if (entry.payload.substr(0, 4) == "BSO1") {
        ++opens;
        chapterOpen = id;
        ok = ok && Capture::decode(entry.checkpoint, sample, closed) && closed && sample.hasSourceOffset &&
             sample.totalSeconds >= 10 && sample.primaryBytes + sample.backupBytes >= 20;
      } else if (entry.payload.substr(0, 4) == "BSS1")
        ++sessions;
      else if (entry.payload.substr(0, 4) == "BSA1")
        ++artifacts;
    }
    const auto& chapters = chapterCapture().value();
    ok = ok && chapters.open == chapterOpen && chapters.state == ChapterState::Ready && chapters.current[0];
    LOG_INF("BSC", "Native current-book chapter capture: %s (next=%u end=%u)", ok ? "PASS" : "FAIL", unsigned(chapters.toNextKnown), unsigned(chapters.toEndKnown));
    finish(ok && opens == 1 && artifacts >= 4 && sessions >= 1,
           "native turn/time, raw slots, highlight/bookmark add/remove and close");
  }
  if (!activityManager.isCurrentActivityNamed("EpubReader")) return true;
  auto& reader = *static_cast<EpubReaderActivity*>(activityManager.simulatorCurrentActivity());
  {
    RenderLock lock;
    if (!reader.section || reader.section->isBuilding() || reader.pendingRelayoutReposition) return true;
    if (reader.section->pageCount < 2) return true;
    if (std::getenv("BOOKSYNC_SIZE_GATE")) verifyBookSyncAdmissionReading();
    // Advance both native time sources consistently. The F03 harness already
    // injects monotonic dwell; here the native session UTC start matches it.
    const time_t startedAt = time(nullptr) - 12;
    struct tm utc;
    gmtime_r(&startedAt, &utc);
    ReadingStatsDateTime start{{static_cast<uint16_t>(utc.tm_year + 1900), static_cast<uint8_t>(utc.tm_mon + 1),
                                static_cast<uint8_t>(utc.tm_mday)},
                               static_cast<uint8_t>(utc.tm_hour),
                               static_cast<uint8_t>(utc.tm_min),
                               static_cast<uint8_t>(utc.tm_sec)};
    reader.readingSessionJournal.start(start, 48);
    reader.pageShownAtMs = millis() - 12000UL;
    reader.pageTurn(true, "capture_fixture");
    reader.flushDeferredReadingStats(false);
    if (reader.sessionReadingSeconds < 10) finish(false, "native reading timer did not accept dwell");
    if (!reader.saveProgress(reader.currentSpineIndex, reader.section->currentPage, reader.section->pageCount))
      finish(false, "native save");
    reader.observeBookSync();
    if (strcmp(mode, "cut") == 0) {
      if (std::getenv("BOOKSYNC_SIZE_GATE"))
        LOG_INF("F14", "BookSync F14 admission PASS: Home worker drained, reader refused network");
      finish(true, "cut after native turn, without close or sleep");
    }
    const auto added = BOOKMARKS.addBookmark(reader.currentSpineIndex, 0.25f, reader.section->pageCount,
                                             "Synthetic chapter", 1, "Synthetic passage");
    if (added != BookmarkStore::AddResult::Added) finish(false, "native bookmark add");
    if (!BOOKMARKS.removeBookmarkAt(0)) finish(false, "native bookmark removal");
    const auto highlighted =
        CLIPPINGS.addClipping(reader.currentSpineIndex, 0, 0, reader.section->pageCount, 0, 1, 2, "Synthetic chapter",
                              1, "Synthetic passage", UINT16_MAX, 1, 144, 180);
    if (highlighted != ClippingStore::AddResult::Added || !CLIPPINGS.removeClippingAt(0))
      finish(false, "native highlight add/removal");
  }
  activityManager.goHome();
  ++phase;
  return true;
}
