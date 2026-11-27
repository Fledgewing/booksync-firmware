// SPDX-License-Identifier: GPL-3.0-or-later
#include <Memory.h>
#include <gtest/gtest.h>
#include <nvs.h>

#include <fstream>
#include <iterator>

#include "booksync/import/Settings.h"
using namespace booksync::import;
namespace {
constexpr booksync::storage::PublishPaths backup{"/.booksync/import/settings/native.raw",
                                                 "/.booksync/import/settings/native.raw.tmp",
                                                 "/.booksync/import/settings/native.raw.bak"};
std::string fixture(const char* path) {
  std::ifstream file(std::string(LEGACY_FIXTURES) + "/" + path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), {}};
}
void seed(const std::string& bytes) {
  card = {};
  card.files["/.crosspoint/crossink-settings.json"] = bytes;
  card.disk = card.files;
}
}  // namespace
TEST(SettingsImport, F03NativeEnumsAndExactRawBackupAcrossTwoBoots) {
  const auto bytes = fixture("sd/.crosspoint/crossink-settings.json");
  seed(bytes);
  Settings first;
  ASSERT_EQ(first.readFile(Source::Native, backup), Status::Ready);
  EXPECT_EQ(first.values()["fontSize"].as<int>(), 2);
  JsonDocument inventory;
  ASSERT_FALSE(deserializeJson(inventory, fixture("inventory.json")));
  std::string actual;
  constexpr char hex[] = "0123456789abcdef";
  for (auto byte : first.fingerprint()) {
    actual += hex[byte >> 4];
    actual += hex[byte & 15];
  }
  for (auto row : inventory["files"].as<JsonArray>())
    if (row["path"] == "sd/.crosspoint/crossink-settings.json") EXPECT_EQ(actual, row["sha256"].as<std::string>());
  EXPECT_EQ(first.sourceBytes(), bytes.size());
  EXPECT_EQ(first.enumValue("publisherFonts").value, 2);
  EXPECT_EQ(first.enumValue("fontSize").value, 14);
  EXPECT_EQ(first.enumValue("fontSize").raw, 2);
  EXPECT_EQ(first.enumValue("characterSpacing").value, -5);
  const auto files = card.files;
  Settings second;
  ASSERT_EQ(second.readFile(Source::Native, backup), Status::Ready);
  EXPECT_EQ(card.files, files);
  EXPECT_EQ(card.files[backup.current], bytes);
  EXPECT_EQ(card.files["/.crosspoint/crossink-settings.json"], bytes);
  EXPECT_EQ(card.handles, 0);
}
TEST(SettingsImport, UnknownEnumAndFieldAreUnresolvedAndRetained) {
  const auto bytes = fixture("variants/settings/unknown.json");
  seed(bytes);
  Settings settings;
  EXPECT_EQ(settings.readFile(Source::Native, backup), Status::Unresolved);
  EXPECT_EQ(settings.enumValue("fontSize").status, EnumStatus::Unknown);
  EXPECT_EQ(settings.values()["futureOption"]["raw"].as<int>(), 17);
  EXPECT_EQ(card.files[backup.current], bytes);
}
TEST(SettingsImport, CorruptAndFutureSourcesAreRetainedWithoutCandidate) {
  for (const auto& raw : {fixture("variants/settings/truncated.json"), std::string("{\"version\":99,\"fontSize\":2}"),
                          std::string("{}garbage"), std::string("[]")}) {
    seed(raw);
    Settings settings;
    const auto status = settings.readFile(Source::Native, backup);
    EXPECT_TRUE(status == Status::Corrupt || status == Status::Unsupported);
    EXPECT_TRUE(settings.values().isNull());
    EXPECT_EQ(card.files[backup.current], raw);
    EXPECT_EQ(card.files["/.crosspoint/crossink-settings.json"], raw);
  }
}
TEST(SettingsImport, OldPathIsReadOnlyAndNeverRewritten) {
  card = {};
  const auto raw = fixture("variants/settings/settings.json");
  card.files["/.crosspoint/settings.json"] = raw;
  Settings settings;
  EXPECT_EQ(settings.readFile(Source::OldNative, backup), Status::Ready);
  EXPECT_EQ(card.files["/.crosspoint/settings.json"], raw);
  EXPECT_FALSE(Storage.exists("/.crosspoint/crossink-settings.json"));
}
TEST(SettingsImport, SleepAndPlainOptionValuesKeepWireMeaning) {
  card = {};
  card.files["/.crosspoint/orbify_sleep/settings.json"] = fixture("sd/.crosspoint/orbify_sleep/settings.json");
  Settings settings;
  EXPECT_EQ(settings.readFile(Source::Sleep, backup), Status::Ready);
  EXPECT_EQ(settings.enumValue("style").value, 1);
  EXPECT_EQ(settings.enumValue("corner").value, 2);
  EXPECT_STREQ(settings.values()["leaveScopes"].as<const char*>(), "scope:17");
  card = {};
  card.files["/.crosspoint/orbify/wifi_idle_min"] = "5";
  EXPECT_EQ(settings.readFile(Source::IdleMinutes, backup), Status::Ready);
  EXPECT_EQ(settings.optionValue(), 5);
}
TEST(SettingsImport, PinRetainsOpaqueSaltAndHashWithoutDerivation) {
  const auto data = fixture("nvs/entries.json");
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, data));
  std::string raw;
  for (auto entry : doc["entries"].as<JsonArray>())
    if (entry["namespace"] == "crossink_pin") {
      const char* hex = entry["hex"];
      for (size_t i = 0; hex[i]; i += 2)
        raw.push_back(static_cast<char>(std::stoi(std::string(hex + i, 2), nullptr, 16)));
    }
  ASSERT_EQ(raw.size(), 48u);
  card = {};
  Settings settings;
  EXPECT_EQ(settings.readPin(SettingsInput::memory(raw.data(), raw.size()), backup), Status::Ready);
  EXPECT_EQ(memcmp(settings.pin().data(), raw.data(), raw.size()), 0);
  EXPECT_EQ(card.files[backup.current], raw);
  const auto before = card.files;
  EXPECT_EQ(settings.readPin(SettingsInput::memory(raw.data(), raw.size()), backup), Status::Ready);
  EXPECT_EQ(card.files, before);
}
TEST(SettingsImport, ExistingDifferentBackupBlocksWithoutReplacingAnything) {
  seed("{\"fontSize\":2}");
  card.files[backup.current] = "original retained";
  const auto before = card.files;
  Settings settings;
  EXPECT_EQ(settings.readFile(Source::Native, backup), Status::BackupConflict);
  EXPECT_EQ(card.files, before);
  EXPECT_TRUE(settings.values().isNull());
}
TEST(SettingsImport, RefusesLegacyOrTraversalBackupDestinations) {
  seed("{}");
  const auto before = card.files;
  Settings settings;
  for (auto path : {"/.crosspoint/crossink-settings.json", "/.booksync/import/settings/../native",
                    "/.booksync/import/settings/sub/raw"}) {
    const std::string tmp = std::string(path) + ".tmp", bak = std::string(path) + ".bak";
    EXPECT_EQ(settings.readFile(Source::Native, {path, tmp.c_str(), bak.c_str()}), Status::BackupConflict);
    EXPECT_EQ(card.files, before);
  }
}
TEST(SettingsImport, FullAndReadOnlyNeverExposeCandidateOrChangeSource) {
  for (bool full : {false, true}) {
    seed("{\"fontSize\":2}");
    const auto before = card.files;
    card.full = full;
    card.readOnly = !full;
    Settings settings;
    EXPECT_EQ(settings.readFile(Source::Native, backup), Status::IoError);
    EXPECT_EQ(card.files, before);
    EXPECT_TRUE(settings.values().isNull());
  }
}
TEST(SettingsImport, PublicationCutAndRestartIsIdempotent) {
  const auto raw = fixture("sd/.crosspoint/crossink-settings.json");
  seed(raw);
  {
    Settings settings;
    ASSERT_EQ(settings.readFile(Source::Native, backup), Status::Ready);
  }
  const int count = card.events.size();
  for (int cut = 0; cut < count; ++cut) {
    seed(raw);
    card.stop = cut;
    {
      Settings settings;
      EXPECT_THROW(settings.readFile(Source::Native, backup), PowerCut);
    }
    card.restart();
    Settings restarted;
    ASSERT_EQ(restarted.readFile(Source::Native, backup), Status::Ready) << cut;
    EXPECT_EQ(card.files["/.crosspoint/crossink-settings.json"], raw);
    EXPECT_EQ(card.files[backup.current], raw);
  }
}

TEST(SettingsImport, NvsPinUsesExactReadonlyNamespaceAndNeverClearsOrRegenerates) {
  card = {};
  fakeNvs = {};
  fakeNvs.blob.resize(48);
  for (size_t i = 0; i < fakeNvs.blob.size(); ++i) fakeNvs.blob[i] = uint8_t(i);
  const auto original = fakeNvs.blob;
  Settings settings;
  EXPECT_EQ(settings.readNvsPin(backup), Status::Ready);
  EXPECT_EQ(settings.readNvsPin(backup), Status::Ready);
  EXPECT_EQ(fakeNvs.name, "crossink_pin");
  EXPECT_EQ(fakeNvs.key, "v1");
  EXPECT_EQ(fakeNvs.mode, NVS_READONLY);
  EXPECT_EQ(fakeNvs.opens, 2u);
  EXPECT_EQ(fakeNvs.closes, 2u);
  EXPECT_EQ(fakeNvs.blob, original);
  EXPECT_EQ(card.files[backup.current], std::string(original.begin(), original.end()));
}
TEST(SettingsImport, MissingCorruptAndOversizedPinNeverBecomesUnsetOrAReplacement) {
  for (const size_t length : {size_t(0), size_t(47), size_t(60), size_t(65), size_t(Settings::ArenaBytes + 1)}) {
    card = {};
    fakeNvs = {};
    fakeNvs.blob.resize(length, 17);
    const auto original = fakeNvs.blob;
    Settings settings;
    EXPECT_EQ(settings.readNvsPin(backup), Status::Unsupported);
    EXPECT_EQ(fakeNvs.blob, original);
    EXPECT_EQ(fakeNvs.opens, fakeNvs.closes);
    if (length <= Settings::ArenaBytes)
      EXPECT_EQ(card.files[backup.current], std::string(original.begin(), original.end()));
  }
  card = {};
  fakeNvs = {};
  fakeNvs.missingKey = true;
  Settings settings;
  EXPECT_EQ(settings.readNvsPin(backup), Status::Missing);
  EXPECT_TRUE(card.files.empty());
}
TEST(SettingsImport, BoundsAndUnknownOptionsNeverReplaceLegacyBytes) {
  for (const auto& raw :
       {std::string("{\"fontSize\":null}"), std::string("{\"fontSize\":2.5}"), std::string("{\"publisherFonts\":3}")}) {
    seed(raw);
    Settings settings;
    EXPECT_EQ(settings.readFile(Source::Native, backup), Status::Unresolved);
    EXPECT_EQ(card.files[backup.current], raw);
  }
  seed(std::string(Settings::MaxJsonBytes + 1, ' '));
  Settings settings;
  EXPECT_EQ(settings.readFile(Source::Native, backup), Status::Unsupported);
  EXPECT_EQ(card.files[backup.current], card.files["/.crosspoint/crossink-settings.json"]);
  card = {};
  card.files["/.crosspoint/orbify/wifi_idle_min"] = "999";
  EXPECT_EQ(settings.readFile(Source::IdleMinutes, backup), Status::Unresolved);
}
TEST(SettingsImport, JsonArenaExhaustionRetainsRawAndWorkspaceCanBeReused) {
  std::string raw = "{\"future\":[";
  for (int i = 0; i < 1800; ++i) {
    if (i) raw += ',';
    raw += "{}";
  }
  raw += "]}";
  seed(raw);
  Settings settings;
  EXPECT_EQ(settings.readFile(Source::Native, backup), Status::Unsupported);
  EXPECT_LE(settings.arenaPeak(), Settings::ArenaBytes);
  EXPECT_TRUE(settings.values().isNull());
  EXPECT_EQ(card.files[backup.current], raw);
  seed("{\"fontSize\":2}");
  EXPECT_EQ(settings.readFile(Source::Native, backup), Status::Ready);
  EXPECT_EQ(settings.enumValue("fontSize").value, 14);
}
TEST(SettingsImport, OldBinaryAndBackupAreInventoriedWithoutGuessingFormat) {
  for (const auto source : {Source::OldBinary, Source::OldBinaryBackup, Source::Language, Source::LanguageBackup}) {
    card = {};
    const std::string raw("\x02\x01\xff\x00", 4);
    card.files[Settings::sourcePath(source)] = raw;
    Settings settings;
    EXPECT_EQ(settings.readFile(source, backup), Status::Unsupported);
    EXPECT_EQ(card.files[Settings::sourcePath(source)], raw);
    EXPECT_EQ(card.files[backup.current], raw);
    EXPECT_TRUE(settings.values().isNull());
  }
  card = {};
  card.files[Settings::sourcePath(Source::NativeBackup)] = fixture("sd/.crosspoint/crossink-settings.json.bak");
  Settings settings;
  EXPECT_EQ(settings.readFile(Source::NativeBackup, backup), Status::Ready);
  EXPECT_FALSE(Storage.exists(Settings::sourcePath(Source::Native)));
}
TEST(SettingsImport, LegacyFontStepsAndNonOrdinalEnumValuesAreNotClamped) {
  seed(
      R"({"fontSize":2,"sdFontFamilyName":"Synthetic","sdFontSizeRange":3,"statusBarProgressBar":2,"statusBarTitle":2,"publisherFonts":2,"wordSpacing":8,"characterSpacing":10})");
  Settings settings;
  ASSERT_EQ(settings.readFile(Source::Native, backup), Status::Ready);
  EXPECT_EQ(settings.enumValue("fontSize").raw, 2);
  EXPECT_EQ(settings.enumValue("fontSize").value, 10);
  EXPECT_EQ(settings.enumValue("sdFontSizeRange").raw, 3);
  EXPECT_EQ(settings.enumValue("sdFontSizeRange").value, 4);
  EXPECT_EQ(settings.enumValue("statusBarProgressBar").value, 2);  // hide, not the first UI choice's index
  EXPECT_EQ(settings.enumValue("statusBarTitle").value, 2);        // hide
  EXPECT_EQ(settings.enumValue("wordSpacing").value, -4);
  EXPECT_EQ(settings.enumValue("characterSpacing").value, 5);
}
TEST(SettingsImport, EveryIoFailureLeavesSourceAndRequiresVerifiedBackup) {
  const auto raw = fixture("sd/.crosspoint/crossink-settings.json");
  seed(raw);
  {
    Settings settings;
    ASSERT_EQ(settings.readFile(Source::Native, backup), Status::Ready);
  }
  const auto count = card.events.size();
  for (size_t failure = 0; failure < count; ++failure) {
    seed(raw);
    card.fail = failure;
    Settings settings;
    const auto result = settings.readFile(Source::Native, backup);
    EXPECT_EQ(card.files[Settings::sourcePath(Source::Native)], raw);
    if (result == Status::Ready)
      EXPECT_EQ(card.files[backup.current], raw);
    else
      EXPECT_TRUE(settings.values().isNull());
    EXPECT_EQ(card.handles, 0);
  }
}
TEST(SettingsImport, ExtendedLegacyCardFontStepsArePreserved) {
  seed(R"({"fontSize":6,"sdFontFamilyName":"Synthetic","sdFontSizeRange":4})");
  Settings settings;
  ASSERT_EQ(settings.readFile(Source::Native, backup), Status::Ready);
  EXPECT_EQ(settings.enumValue("fontSize").raw, 6);
  EXPECT_EQ(settings.enumValue("fontSize").value, 18);
  seed(R"({"fontSize":6})");
  EXPECT_EQ(settings.readFile(Source::Native, backup), Status::Unresolved);
}

extern "C" Status booksync_settings_import_link(Source source, booksync::storage::PublishPaths paths, bool pin);
TEST(SettingsImport, FullLinkAnchorIncludesFallibleWorkspaceLifetime) {
  const auto bytes = fixture("sd/.crosspoint/crossink-settings.json");
  seed(bytes);
  const auto before = card.files;
  settings_memory_fixture::attempts = 0;
  settings_memory_fixture::fail = true;
  EXPECT_EQ(booksync_settings_import_link(Source::Native, backup, false), Status::IoError);
  settings_memory_fixture::fail = false;
  EXPECT_EQ(card.files, before);
  EXPECT_EQ(settings_memory_fixture::attempts, 1u);
  EXPECT_EQ(card.handles, 0);
  EXPECT_EQ(booksync_settings_import_link(Source::Native, backup, false), Status::Ready);
  const auto retained = card.files;
  EXPECT_EQ(booksync_settings_import_link(Source::Native, backup, false), Status::Ready);
  EXPECT_EQ(card.files, retained);
  EXPECT_EQ(card.files[Settings::sourcePath(Source::Native)], bytes);
  EXPECT_EQ(card.files[backup.current], bytes);
  EXPECT_EQ(card.handles, 0);
}
