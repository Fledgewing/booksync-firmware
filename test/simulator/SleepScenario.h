// SPDX-License-Identifier: GPL-3.0-or-later
// Included in the existing reader-friend harness; never a production UI path.
static bool sleepScenarioTick() {
  const char* mode = std::getenv("BOOKSYNC_SLEEP_SCENARIO");
  if (!mode) return false;
  using namespace booksync;
  using namespace booksync::storage;
  static unsigned phase = 0, ticks = 0;
  const bool resuming = !std::strcmp(mode, "resume") || !std::strcmp(mode, "cold");
  const auto finish = [](bool ok, const char* detail) {
    if (ok)
      LOG_INF("SLEEPTEST", "BookSync sleep scenario PASS: %s", detail);
    else
      LOG_ERR("SLEEPTEST", "BookSync sleep scenario FAIL: %s", detail);
    std::fflush(nullptr);
    std::_Exit(ok ? 0 : 1);
  };
  if (++ticks > 30000) finish(false, "reader failed to settle");
  if (!phase) {
    SETTINGS.pluginsEnabled = 0;
    if (!std::strcmp(mode, "cold")) {
      // An unclean reader reboot intentionally lands at Home. Preserve that
      // native crash guard, then exercise an explicit saved-book reopen.
      if (!activityManager.isHomeActivity() || homeLibrary().sleepView().captured)
        finish(false, "cold boot did not preserve native Home/crash ownership");
      activityManager.goToReader("/Books/Synthetic.epub", true);
    } else if (!resuming) {
      SETTINGS.sleepScreen = CrossPointSettings::BLANK;
      activityManager.goToReader("/Books/Synthetic.epub", true);
    }
    ++phase;
    return true;
  }
  if (!activityManager.isCurrentActivityNamed("EpubReader")) return true;
  auto& reader = *static_cast<EpubReaderActivity*>(activityManager.simulatorCurrentActivity());
  {
    RenderLock lock;
    if (!reader.section || reader.section->isBuilding() || reader.pendingRelayoutReposition) return true;
    if (reader.section->pageCount < 2) return true;
    if (resuming) {
      auto saved = Storage.open("/sleep-expected", O_RDONLY);
      uint32_t expected[2]{};
      const bool read = saved && saved.read(expected, sizeof(expected)) == sizeof(expected);
      saved.close();
      finish(read && expected[0] == reader.currentSpineIndex && expected[1] == uint32_t(reader.section->currentPage) &&
                 !homeLibrary().sleepView().captured,
             "reboot resumes native anchor with no old sleep ownership");
    }
    if (phase == 1) {
      reader.pageTurn(true, "sleep_fixture");
      ++phase;
      return true;
    }
    // Native persistence deliberately follows the last painted page, not a
    // queued turn. Wait for the real render/debouncer seam before sleeping.
    const uint32_t shown = (uint32_t(reader.currentSpineIndex) << 16) | uint16_t(reader.section->currentPage);
    if (reader.pageBufferStale.load() || reader.progressSaveDebouncer.lastObservedPosition() != shown) return true;
  }
  // This is the real reader hook, under its own RenderLock, before any drain.
  // Plugins are deliberately disabled: capture is independent of subscriptions.
  activityManager.prepareForSleep();
  {
    RenderLock lock;
    LifecycleJournal journal(
        {"/.booksync/reader.journal", "/.booksync/reader.journal.tmp", "/.booksync/reader.journal.bak"});
    bool ok = journal.recover() == JournalResult::Ok, found = false;
    uint64_t id = 0;
    char bytes[2048];
    JournalEntry entry;
    booksync::reader::Sample sample;
    bool closed = true;
    while (ok && journal.next("native", "local-unbound", id, id) == JournalResult::Ok) {
      ok = journal.read(id, entry, bytes, sizeof(bytes)) == JournalResult::Ok;
      if (entry.payload.substr(0, 4) == "BSO1" && booksync::reader::Capture::decode(entry.checkpoint, sample, closed))
        found |= !closed && sample.page == uint32_t(reader.section->currentPage) && sample.primaryBytes >= 20;
    }
    if (!ok || !found) finish(false, "reader observation not durable before drain");
    auto file = Storage.open("/sleep-expected", O_WRONLY | O_CREAT | O_TRUNC);
    const uint32_t expected[]{reader.currentSpineIndex, uint32_t(reader.section->currentPage)};
    const bool saved = file && file.write(expected, sizeof(expected)) == sizeof(expected) && file.sync();
    file.close();
    if (!saved) finish(false, "expected anchor could not be saved");
  }
  // The pinned HAL waits in sleep and execs on a key; it is not a no-op.
  // A real power wake must start a fresh process and retire volatile ownership.
  const bool returning = !std::strcmp(mode, "return");
  SDL_Event wake{};
  wake.type = returning ? SDL_QUIT : SDL_KEYDOWN;
  wake.key.keysym.scancode = SDL_SCANCODE_P;
  if (SDL_PushEvent(&wake) != 1) finish(false, "could not queue native wake event");
  if (!returning) setenv("BOOKSYNC_SLEEP_SCENARIO", "resume", 1);
  LOG_INF("SLEEPTEST", "BookSync sleep scenario PASS: durable capture before native sleep");
  std::fflush(nullptr);
  enterDeepSleep(false);
  finish(!homeLibrary().sleepView().captured && !reminderSceneHidden(),
         "native sleep returns without stale sleep or reminder ownership");
  return true;
}
