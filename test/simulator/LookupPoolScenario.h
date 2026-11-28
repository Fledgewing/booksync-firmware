// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "booksync/services/SavedLookupPoolSource.h"
inline void runLookupPoolScenario() {
  const char* mode = std::getenv("BOOKSYNC_LOOKUP_POOL_SCENARIO");
  if (!mode) return;
  using namespace booksync;
  static SavedLookupPoolSource source;
  static LookupPool pool;
  bool ok = true;
  const auto write = [&](const char* folder, const char* suffix, const std::string& bytes) {
    Storage.mkdir("/.crosspoint");
    char directory[96];
    std::snprintf(directory, sizeof(directory), "/.crosspoint/%s", folder);
    Storage.mkdir(directory);
    char path[160];
    std::snprintf(path, sizeof(path), "%s/dictionary_history.txt%s", directory, suffix);
    auto file = Storage.open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (!file) return false;
    const bool saved = file.write(bytes.data(), bytes.size()) == bytes.size() && file.sync();
    const bool closed = file.close();
    return saved && closed;
  };
  const std::string scenario(mode);
  LookupPoolState expected = LookupPoolState::Ready;
  const char* word = "river";
  unsigned total = 3;
  if (scenario == "known") {
    ok = write("epub_3", "", "river|D\nwind|X\n") && write("txt_1", "", "cloud|T\nriver|D\n") &&
         write("ignored_4", "", "unrelated|D\n");
  } else if (scenario == "reverse") {
    ok = write("txt_1", "", "wind|D\nriver|D\n") && write("epub_3", "", "river|D\ncloud|D\n");
  } else if (scenario == "empty") {
    ok = write("epub_3", "", "");
    expected = LookupPoolState::Empty;
    word = "";
    total = 0;
  } else if (scenario == "missing") {
    expected = LookupPoolState::NotSaved;
    word = "";
    total = 0;
  } else if (scenario == "backup") {
    ok = write("xtc_5", ".bak", "river|S\n") && write("xtc_5", ".tmp", "staging|D\n");
    total = 1;
  } else if (scenario == "torn") {
    ok = write("epub_3", "", "river|D\nunfinished|");
    expected = LookupPoolState::Unavailable;
    word = "";
    total = 0;
  } else if (scenario == "malformed") {
    ok = write("epub_3", "", "river|Q\n");
    expected = LookupPoolState::Unavailable;
    word = "";
    total = 0;
  } else if (scenario == "unicode") {
    ok = write("epub_3", "", "𐐀水|S\n");
    word = "𐐀水";
    total = 1;
  } else if (scenario == "retired") {
    ok = write("epub_3", "", "river|D\n");
    expected = LookupPoolState::Unavailable;
    word = "";
    total = 0;
  } else {
    ok = false;
  }
  source.begin();
  pool.begin(source.ports(), 1);
  if (scenario == "retired") source.retire();
  for (unsigned n = 0; n < 100000 && pool.busy(); ++n) pool.tick();
  const auto& out = pool.result();
  ok = ok && !pool.busy() && out.state == expected && out.total == total && std::strcmp(out.word.data(), word) == 0 &&
       pool.retainedAllocationBytes() == 0;
  LOG_INF("BSW", "BookSync lookup pool %s: mode=%s words=%u bytes=%u scratch=36864", ok ? "PASS" : "FAIL", mode,
          unsigned(out.total), unsigned(source.consumedBytes()));
  std::fflush(nullptr);
  std::_Exit(ok ? 0 : 1);
}
