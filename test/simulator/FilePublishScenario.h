// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>
// Included only by the native simulator harness, after Storage.begin().
#include <cstdlib>
#include <cstring>

#include "booksync/storage/FilePublish.h"

inline void runFilePublishScenario() {
  const char* expected = std::getenv("BOOKSYNC_PUBLICATION_EXPECT");
  if (!expected) return;
  using namespace booksync::storage;
  constexpr PublishPaths paths{"/publication/state", "/publication/state.tmp", "/publication/state.bak"};
  const auto validator = [](FsFile& file, void*) {
    char bytes[12];
    return file.fileSize() == sizeof(bytes) && file.read(bytes, sizeof(bytes)) == sizeof(bytes) &&
           (memcmp(bytes, "complete-old", sizeof(bytes)) == 0 || memcmp(bytes, "complete-new", sizeof(bytes)) == 0);
  };
  const auto selected = FilePublish::select(paths, validator);
  bool ok = false;
  if (strcmp(expected, "missing") == 0) {
    ok = selected == PublishedFile::Missing;
  } else if (selected == PublishedFile::Current || selected == PublishedFile::Backup) {
    FsFile file = Storage.open(selected == PublishedFile::Current ? paths.current : paths.backup, O_RDONLY);
    char bytes[12];
    ok = file && file.read(bytes, sizeof(bytes)) == sizeof(bytes) && memcmp(bytes, expected, sizeof(bytes)) == 0;
    if (file && !file.close()) ok = false;
  }
  if (ok && std::getenv("BOOKSYNC_PUBLICATION_WRITE")) {
    FilePublish writer(paths, validator);
    ok = writer.begin(12) && writer.write("complete-", 9) && writer.write("new", 3) && writer.finish();
  }
  if (ok)
    LOG_INF("BSP", "BookSync publication scenario PASS");
  else
    LOG_ERR("BSP", "BookSync publication scenario FAIL");
  std::exit(ok ? 0 : 1);
}
