// SPDX-License-Identifier: GPL-3.0-or-later
// Included inside the retained simulator's existing reader-friend test harness.
// All access is simulator-only; production reader navigation/save code is unchanged.
static bool locatorScenarioTick() {
  if (!std::getenv("BOOKSYNC_LOCATOR_SCENARIO")) return false;
  using namespace booksync::storage;
  using namespace booksync::reader;
  static constexpr SourceIdentity source{{{1}}, {{2}}, {{3}}};
  static char raw[20];
  static unsigned phase = 0;
  static unsigned ticks = 0;
  static int originalWidth = 0;
  static uint32_t savedOffset = 0;
  static uint32_t reflowOffset = 0;
  static int savedPage = 0;
  const auto finish = [](const bool ok, const char* detail) {
    if (ok)
      LOG_INF("BSL", "BookSync locator scenario PASS: %s", detail);
    else
      LOG_ERR("BSL", "BookSync locator scenario FAIL: %s", detail);
    std::fflush(nullptr);
    std::_Exit(ok ? 0 : 1);
  };
  if (++ticks > 30000) finish(false, "native reader did not settle");
  constexpr const char* book = "/Books/Synthetic.epub";
  if (phase == 0) {
    // Same raw F03 fixture used by the retained host production-loader test.
    auto input = Storage.open("/f08-anchor.bin", O_RDONLY);
    bool ok = input && input.fileSize() == sizeof(raw) && input.read(raw, sizeof(raw)) == sizeof(raw);
    input.close();
    progress_record::Fields anchor;
    ok = ok && epubAnchor({source, NativeEpubProgress, 1, {raw, sizeof(raw)}}, source, anchor) == ReplayStatus::Ready &&
         anchor.spineIndex == 1 && anchor.visibleTextOffset == 144;
    // A canonical alias also resolves before any edition/file is known.
    const BookAliasBindingView remoteOnly{{"server-a", "account-a"}, {"book", "opaque-key"}, source.book};
    ok = ok && resolveBookAlias(remoteOnly.scope, remoteOnly.alias, &remoteOnly, 1).book == source.book;
    // Namespace and collision policy also executes in every device simulator.
    BindingView rows[]{{{"server-a", "account-a"}, {"book", "opaque-key"}, book, source},
                       {{"server-a", "account-b"}, {"book", "opaque-key"}, book, {{{4}}, {{5}}, {{6}}}}};
    ok = ok && resolveAlias(rows[0].scope, rows[0].alias, rows, 2).source == source;
    rows[1].scope = rows[0].scope;
    ok = ok && resolveAlias(rows[0].scope, rows[0].alias, rows, 2).status == MatchStatus::Ambiguous &&
         resolvePath(rows[0].scope, book, source.revision, rows, 2).source == source;
    if (!ok) finish(false, "fixture or identity policy");
    const auto cache = Epub::cachePathForFilePath(book, "/.crosspoint");
    if (!Storage.mkdir(cache.c_str())) finish(false, "cache directory");
    auto output = Storage.open((cache + "/progress.bin").c_str(), O_WRONLY | O_CREAT | O_TRUNC);
    ok = output && output.write(raw, sizeof(raw)) == sizeof(raw);
    if (!output.close()) ok = false;
    if (!ok) finish(false, "fixture publication");
    activityManager.goToReader(book, true);
    ++phase;
    return true;
  }
  if (phase == 3) {
    if (!activityManager.isHomeActivity()) return true;
    activityManager.goToReader(book, true);
    ++phase;
    return true;
  }
  if (!activityManager.isCurrentActivityNamed("EpubReader")) return true;
  auto& reader = *static_cast<EpubReaderActivity*>(activityManager.simulatorCurrentActivity());
  {
    RenderLock lock;
    if (!reader.section || reader.section->isBuilding() || reader.pendingRelayoutReposition) return true;
    if (reader.currentSpineIndex != 1) finish(false, "native spine changed");
    const auto contains = [&](uint32_t offset) {
      const int page = reader.section->currentPage;
      const auto begin = reader.section->getVisibleTextOffsetForPage(page);
      const auto end =
          page + 1 < reader.section->pageCount ? reader.section->getVisibleTextOffsetForPage(page + 1) : std::nullopt;
      return begin && *begin <= offset && (!end || *end > offset);
    };
    if (phase == 1) {
      if (!contains(144)) finish(false, "F03 native anchor not restored");
      // Rotate through the existing native operation: actual layout dimensions
      // change, rather than substituting a fake page-number conversion.
      if (reader.section->pageCount < 2) finish(false, "fixture chapter has no second page");
      reader.section->currentPage = 1;
      const auto anchor = reader.section->getVisibleTextOffsetForPage(1);
      if (!anchor || *anchor == 0) finish(false, "nonzero native reflow anchor");
      reflowOffset = *anchor;
      originalWidth = reader.buildViewportWidth;
    } else if (phase == 2) {
      if (reader.buildViewportWidth == originalWidth) return true;
      const auto offset = reader.section->getVisibleTextOffsetForPage(reader.section->currentPage);
      if (!offset || !contains(reflowOffset)) finish(false, "reflow lost original passage");
      savedOffset = *offset;
      savedPage = reader.section->currentPage;
      if (!reader.saveProgress(1, savedPage, reader.section->pageCount)) finish(false, "native save");
      EpubReaderUtils::Progress reopened;
      if (!EpubReaderUtils::loadProgress(*reader.epub, reopened) || !reopened.hasVisibleTextOffset ||
          reopened.visibleTextOffset != savedOffset)
        finish(false, "saved native offset");
      // Preserve the actual newest native slot, including sequence and CRC.
      const auto path = reader.epub->getCachePath() + "/progress.bin";
      const auto primary = EpubReaderUtils::readProgressSlot("BSL", path);
      const auto backup = EpubReaderUtils::readProgressSlot("BSL", path + ".bak");
      auto file = Storage.open(
          (progress_record::newest(primary, backup) == progress_record::PRIMARY ? path : path + ".bak").c_str(),
          O_RDONLY);
      bool ok = file && file.fileSize() == sizeof(raw) && file.read(raw, sizeof(raw)) == sizeof(raw);
      file.close();
      progress_record::Fields anchor;
      ok = ok &&
           epubAnchor({source, NativeEpubProgress, 1, {raw, sizeof(raw)}}, source, anchor) == ReplayStatus::Ready &&
           anchor.visibleTextOffset == savedOffset;
      if (!ok) finish(false, "raw capture");
      LOG_INF("BSL", "Native reflow saved spine=1 offset=%lu page=%d viewport=%d->%d",
              static_cast<unsigned long>(savedOffset), savedPage, originalWidth, reader.buildViewportWidth);
    } else if (phase == 4) {
      if (reader.buildViewportWidth == originalWidth || !contains(savedOffset) || !contains(reflowOffset))
        finish(false, "reopen lost native anchor or reflow settings");
      finish(true, "F03 raw anchor, native reflow, close and reopen; no transport");
    }
  }
  if (phase == 1) {
    reader.applyOrientation(SETTINGS.orientation == CrossPointSettings::PORTRAIT ? CrossPointSettings::LANDSCAPE_CW
                                                                                 : CrossPointSettings::PORTRAIT);
    reader.requestUpdate();
    ++phase;
  } else if (phase == 2) {
    activityManager.goHome();
    ++phase;
  }
  return true;
}
