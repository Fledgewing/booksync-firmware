// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>

#include "booksync/import/ReadingAnnotations.h"
inline void runReadingAnnotationsScenario() {
  const char* boot = std::getenv("BOOKSYNC_ANNOTATIONS_SCENARIO");
  if (!boot) return;
  using namespace booksync::import;
  static ReadingAnnotations importer;
  static const AnnotationFormat formats[] = {AnnotationFormat::Clippings,       AnnotationFormat::Bookmarks,
                                             AnnotationFormat::Anchors,         AnnotationFormat::BookmarkAnchors,
                                             AnnotationFormat::BookmarkAnchors, AnnotationFormat::Downloads,
                                             AnnotationFormat::Downloads};
  bool ok = Storage.mkdir("/.booksync/import/annotations", true);
  for (unsigned i = 0; i < 7; ++i) {
    char source[16], current[48], tmp[52], backup[52];
    snprintf(source, sizeof(source), "/source%u", i);
    snprintf(current, sizeof(current), "/.booksync/import/annotations/archive%u", i);
    snprintf(tmp, sizeof(tmp), "%s.tmp", current);
    snprintf(backup, sizeof(backup), "%s.bak", current);
    const auto result = importer.preserve(source, {current, tmp, backup}, formats[i],
                                          "synthetic;0.11;account-epoch:7;backup-preserved");
    ok = ok && result == (*boot == '1' ? AnnotationStatus::Ready : AnnotationStatus::AlreadyPreserved);
  }
  auto invalid = importer.preserve("/unknown",
                                   {"/.booksync/import/annotations/bad", "/.booksync/import/annotations/bad.tmp",
                                    "/.booksync/import/annotations/bad.bak"},
                                   AnnotationFormat::Downloads, "synthetic");
  ok = ok && invalid == AnnotationStatus::Unsupported && !Storage.exists("/.booksync/import/annotations/bad");
  LOG_INF("BSI", "BookSync annotation import %s boot=%s workspace=%u payload-allocations=0", ok ? "PASS" : "FAIL", boot,
          unsigned(sizeof(importer)));
  std::fflush(nullptr);
  std::_Exit(ok ? 0 : 1);
}
