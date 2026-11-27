// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Logging.h>

#include <cstdio>
#include <cstdlib>

#include "booksync/import/Settings.h"
inline void runSettingsImportScenario() {
  if (!std::getenv("BOOKSYNC_SETTINGS_IMPORT_SCENARIO")) return;
  using namespace booksync::import;
  static Settings settings;
  constexpr booksync::storage::PublishPaths paths{"/.booksync/import/settings/native.raw",
                                                  "/.booksync/import/settings/native.raw.tmp",
                                                  "/.booksync/import/settings/native.raw.bak"};
  const auto status = settings.readFile(Source::Native, paths);
  const char* expected = std::getenv("BOOKSYNC_SETTINGS_EXPECT");
  bool ok = expected && unsigned(status) == unsigned(std::strtoul(expected, nullptr, 10));
  if (status == Status::Ready)
    ok = ok && settings.enumValue("publisherFonts").value == 2 && settings.enumValue("characterSpacing").value == -5;
  const auto peak = settings.arenaPeak();
  constexpr booksync::storage::PublishPaths pinPaths{"/.booksync/import/settings/pin.raw",
                                                     "/.booksync/import/settings/pin.raw.tmp",
                                                     "/.booksync/import/settings/pin.raw.bak"};
  uint8_t pin[48];
  FsFile pinFile = Storage.open("/pin.fixture", O_RDONLY);
  ok = ok && bool(pinFile);
  if (pinFile) {
    const int n = pinFile.read(pin, sizeof(pin));
    const bool closed = pinFile.close();
    ok = ok && n == sizeof(pin) && closed &&
         settings.readPin(SettingsInput::memory(pin, sizeof(pin)), pinPaths) == Status::Ready;
  }
  LOG_INF("BSI",
          "BookSync settings import scenario %s: workspace=%u arena-peak=%u payload-heap=0 "
          "hardware-heap-and-stack=unrepresented",
          ok ? "PASS" : "FAIL", unsigned(sizeof(Settings)), unsigned(peak));
  std::fflush(nullptr);
  std::_Exit(ok ? 0 : 1);
}
