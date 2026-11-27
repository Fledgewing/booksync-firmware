// SPDX-License-Identifier: GPL-3.0-or-later
// Legacy format references: F03 inventory.json pins CrossInk-Bookorbit settings
// and BookSync Settings/SleepScreen/SettingsPin sources. No old writers reused.
#include "Settings.h"

#include <Logging.h>

#include <algorithm>
#include <cstring>
#if defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#elif defined(SIMULATOR) || defined(BOOKSYNC_IMPORT_HOST)
#include <openssl/sha.h>
#else
#include <mbedtls/sha256.h>
#endif
#if defined(ARDUINO_ARCH_ESP32) && !defined(SIMULATOR)
#include <nvs.h>
#endif

namespace booksync::import {
namespace {
struct alignas(std::max_align_t) Block {
  size_t size;
};
struct Reader {
  SettingsInput input;
  size_t offset = 0;
  bool failed = false;
  uint8_t cache[128]{};
  size_t cached = 0, cursor = 0;
  int read() {
    if (offset == input.bytes) return -1;
    if (cursor == cached) {
      cached = std::min(sizeof(cache), input.bytes - offset);
      cursor = 0;
      if (!input.read(input.context, offset, cache, cached)) {
        failed = true;
        return -1;
      }
    }
    ++offset;
    return cache[cursor++];
  }
  size_t readBytes(char* out, size_t n) {
    size_t i = 0;
    for (; i < n; ++i) {
      const int c = read();
      if (c < 0) break;
      out[i] = static_cast<char>(c);
    }
    return i;
  }
};
bool whitespace(int c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }
size_t fileBytes(FsFile& file) {
#if SIZE_MAX < UINT64_MAX
  const auto n = file.fileSize64();
  return n > SIZE_MAX ? SIZE_MAX : static_cast<size_t>(n);
#else
  return file.fileSize();
#endif
}
SettingsInput fileInput(FsFile& file) {
  return {[](void* p, size_t offset, void* out, size_t n) {
            auto& file = *static_cast<FsFile*>(p);
            return file.seekSet(offset) && file.read(out, n) == static_cast<int>(n);
          },
          &file, fileBytes(file)};
}
bool backupPath(const char* path) {
  constexpr char prefix[] = "/.booksync/import/settings/";
  if (!path || strncmp(path, prefix, sizeof(prefix) - 1)) return false;
  const char* name = path + sizeof(prefix) - 1;
  if (!*name || *name == '.') return false;
  for (; *name; ++name)
    if (!((*name >= 'a' && *name <= 'z') || (*name >= '0' && *name <= '9') || *name == '.' || *name == '_' ||
          *name == '-'))
      return false;
  return true;
}
bool equalFile(FsFile& file, void* context) {
  auto& input = *static_cast<SettingsInput*>(context);
  if (fileBytes(file) != input.bytes) return false;
  uint8_t source[64], saved[64];  // bounded byte comparison, never a hash-only proof
  for (size_t offset = 0; offset < input.bytes;) {
    const size_t n = std::min(sizeof(source), input.bytes - offset);
    if (!input.read(input.context, offset, source, n) || file.read(saved, n) != static_cast<int>(n) ||
        memcmp(source, saved, n))
      return false;
    offset += n;
  }
  return true;
}
// One cold worker call, bounded crypto context plus a 64-byte chunk. The
// context needs slightly over 256 stack bytes on some host crypto libraries;
// no whole-source buffer, key derivation, crypto heap or transport is involved.
bool fingerprint(SettingsInput input, uint8_t* digest) {
#if defined(__APPLE__)
  CC_SHA256_CTX hash;
  bool ok = CC_SHA256_Init(&hash) == 1;
#elif defined(SIMULATOR) || defined(BOOKSYNC_IMPORT_HOST)
  SHA256_CTX hash;
  bool ok = SHA256_Init(&hash) == 1;
#else
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  bool ok = mbedtls_sha256_starts(&hash, 0) == 0;
#endif
  uint8_t chunk[64];
  for (size_t offset = 0; ok && offset < input.bytes;) {
    const size_t n = std::min(sizeof(chunk), input.bytes - offset);
    ok = input.read(input.context, offset, chunk, n);
    if (!ok) break;
#if defined(__APPLE__)
    ok = CC_SHA256_Update(&hash, chunk, static_cast<CC_LONG>(n)) == 1;
#elif defined(SIMULATOR) || defined(BOOKSYNC_IMPORT_HOST)
    ok = SHA256_Update(&hash, chunk, n) == 1;
#else
    ok = mbedtls_sha256_update(&hash, chunk, n) == 0;
#endif
    offset += n;
  }
#if defined(__APPLE__)
  if (ok) ok = CC_SHA256_Final(digest, &hash) == 1;
#elif defined(SIMULATOR) || defined(BOOKSYNC_IMPORT_HOST)
  if (ok) ok = SHA256_Final(digest, &hash) == 1;
#else
  if (ok) ok = mbedtls_sha256_finish(&hash, digest) == 0;
  mbedtls_sha256_free(&hash);
#endif
  return ok;
}
struct Range {
  const char* key;
  int32_t low, high;
};
// Persisted values, independent of a device's currently offered choices. In
// particular publisherFonts=2 is meaningful; characterSpacing=0 means -5.
constexpr Range nativeEnums[] = {{"fontFamily", 0, 1},
                                 {"fontSize", 0, 255},
                                 {"sleepScreen", 0, 12},
                                 {"sleepScreenCoverMode", 0, 1},
                                 {"sleepScreenCoverFilter", 0, 2},
                                 {"menuNavigation", 0, 1},
                                 {"publisherFonts", 0, 2},
                                 {"wordSpacing", 0, 8},
                                 {"characterSpacing", 0, 10},
                                 {"statusBarTextSize", 0, 2},
                                 {"displayStatusBarTextSize", 0, 2},
                                 {"orientation", 0, 3},
                                 {"paragraphAlignment", 0, 4},
                                 {"lineSpacing", 0, 2},
                                 {"sdFontSizeRange", 0, 4},
                                 {"sideButtonLayout", 0, 3},
                                 {"sideButtonLongPress", 0, 3},
                                 {"frontButtonLayout", 0, 3},
                                 {"hideClock", 0, 2},
                                 {"statusBar", 0, 5},
                                 {"dateFormat", 0, 8},
                                 {"dateSeparator", 0, 2},
                                 {"refreshFrequency", 0, 5},
                                 {"uiTheme", 0, 8},
                                 {"uiScale", 0, 1},
                                 {"ruledLines", 0, 3},
                                 {"textDarkness", 0, 2},
                                 {"indexingMethod", 0, 1},
                                 {"frontButtonOrientationAware", 0, 2},
                                 {"tiltPageTurnDirection", 0, 3},
                                 {"homeButtonInReaderEnabled", 0, 1},
                                 {"pageTurnGesture", 0, 4},
                                 {"previousPageGesture", 0, 4},
                                 {"fileBrowserDisplay", 0, 1},
                                 {"statusBarBookPercentageFormat", 0, 2},
                                 {"statusBarProgressBar", 0, 2},
                                 {"statusBarProgressBarThickness", 0, 2},
                                 {"statusBarTitle", 0, 2},
                                 {"statusBarTimeLeft", 0, 2},
                                 {"xtcStatusBarMode", 0, 3},
                                 {"clockFormat", 0, 1},
                                 {"imageRendering", 0, 2},
                                 {"imageGrayscale", 0, 1},
                                 {"twoFingerSwipeUp", 0, 12},
                                 {"twoFingerSwipeDown", 0, 12},
                                 {"twoFingerSwipeLeft", 0, 12},
                                 {"twoFingerSwipeRight", 0, 12},
                                 {"leftEdgeUp", 0, 12},
                                 {"leftEdgeDown", 0, 12},
                                 {"rightEdgeUp", 0, 12},
                                 {"rightEdgeDown", 0, 12}};
constexpr Range sleepEnums[] = {{"style", 0, 1}, {"textPos", 0, 3}, {"corner", 0, 3}};
}  // namespace
SettingsInput SettingsInput::memory(const void* data, size_t bytes) {
  return {[](void* p, size_t offset, void* out, size_t n) {
            if (!p && n) return false;
            memcpy(out, static_cast<const uint8_t*>(p) + offset, n);
            return true;
          },
          const_cast<void*>(data), bytes};
}
void* Settings::Arena::allocate(size_t size) {
  const size_t aligned = (used + alignof(Block) - 1) & ~(alignof(Block) - 1);
  if (aligned > ArenaBytes || size > ArenaBytes - aligned || sizeof(Block) > ArenaBytes - aligned - size)
    return nullptr;
  auto* block = reinterpret_cast<Block*>(bytes + aligned);
  block->size = size;
  used = aligned + sizeof(Block) + size;
  return block + 1;
}
void* Settings::Arena::reallocate(void* pointer, size_t size) {
  if (!pointer) return allocate(size);
  const auto old = (static_cast<Block*>(pointer) - 1)->size;
  if (size <= old) return pointer;
  void* result = allocate(size);
  if (result) memcpy(result, pointer, old);
  return result;
}
void Settings::reset() {
  document_.clear();
  arena_.used = 0;
  retained_ = nullptr;
  sourceBytes_ = 0;
  fingerprint_.fill(0);
  candidate_ = false;
  option_ = 0;
  pin_.fill(0);
}
const char* Settings::sourcePath(Source source) {
  switch (source) {
    case Source::Native:
      return "/.crosspoint/crossink-settings.json";
    case Source::NativeBackup:
      return "/.crosspoint/crossink-settings.json.bak";
    case Source::OldNative:
      return "/.crosspoint/settings.json";
    case Source::Sleep:
      return "/.crosspoint/orbify_sleep/settings.json";
    case Source::StorageLimit:
      return "/.crosspoint/orbify/storage_limit_mb";
    case Source::WifiMode:
      return "/.crosspoint/orbify/wifi_mode";
    case Source::IdleMinutes:
      return "/.crosspoint/orbify/wifi_idle_min";
    case Source::OldBinary:
      return "/.crosspoint/settings.bin";
    case Source::OldBinaryBackup:
      return "/.crosspoint/settings.bin.bak";
    case Source::Language:
      return "/.crosspoint/language.bin";
    case Source::LanguageBackup:
      return "/.crosspoint/language.bin.bak";
  }
  return nullptr;
}
Status Settings::retain(SettingsInput input, storage::PublishPaths paths) {
  if (!backupPath(paths.current) || !backupPath(paths.staging) || !backupPath(paths.backup))
    return Status::BackupConflict;
  if (!::booksync::import::fingerprint(input, fingerprint_.data())) {
    LOG_ERR("BSI", "Source fingerprint read failed");
    return Status::IoError;
  }
  sourceBytes_ = input.bytes;
  // Existing raw evidence is immutable, even when damaged/different. select()
  // validates against every byte of this source, not a truncated fingerprint.
  const auto selected = storage::FilePublish::select(paths, equalFile, &input);
  if (selected == storage::PublishedFile::Current || selected == storage::PublishedFile::Backup) {
    retained_ = selected == storage::PublishedFile::Current ? paths.current : paths.backup;
    return Status::Ready;
  }
  if (selected != storage::PublishedFile::Missing) return Status::BackupConflict;
  storage::FilePublish writer(paths, equalFile, &input);
  if (!writer.begin(input.bytes)) return Status::IoError;
  uint8_t copy[128];
  for (size_t offset = 0; offset < input.bytes;) {
    const size_t n = std::min(sizeof(copy), input.bytes - offset);
    if (!input.read(input.context, offset, copy, n) || !writer.write(copy, n)) return Status::IoError;
    offset += n;
  }
  if (!writer.finish()) return Status::IoError;
  retained_ = paths.current;
  return Status::Ready;
}
EnumValue Settings::enumValue(const char* key) const {
  if (!candidate_ || !key) return {};
  const auto raw = document_[key];
  if (raw.isUnbound()) return {};
  const Range* begin = source_ == Source::Sleep ? sleepEnums : nativeEnums;
  const size_t count = source_ == Source::Sleep ? std::size(sleepEnums) : std::size(nativeEnums);
  for (size_t i = 0; i < count; ++i)
    if (!strcmp(key, begin[i].key)) {
      if (!raw.is<int32_t>()) return {EnumStatus::Unknown};
      const int32_t n = raw.as<int32_t>();
      if (n < begin[i].low || n > begin[i].high) return {EnumStatus::Unknown, n, n};
      int32_t value = n;
      if (!strcmp(key, "sdFontSizeRange") && n == 3) value = 4;
      if (!strcmp(key, "fontSize")) {
        const char* family = document_["sdFontFamilyName"].as<const char*>();
        const bool cardFont = family && *family;
        if (n < 8) {
          if (cardFont) {
            static constexpr uint8_t sizes[][8] = {{8, 9, 10, 12},
                                                   {10, 12, 14, 16},
                                                   {14, 16, 18, 20},
                                                   {8, 9, 10, 12, 14, 16, 18, 20},
                                                   {8, 9, 10, 12, 14, 16, 18, 20}};
            const auto range = document_["sdFontSizeRange"];
            if (!range.isUnbound() && (!range.is<uint8_t>() || range.as<uint8_t>() > 4))
              return {EnumStatus::Unknown, n, n};
            value = sizes[range.isUnbound() ? 1 : range.as<uint8_t>()][n];
            if (!value) return {EnumStatus::Unknown, n, n};
          } else {
            if (n > 3) return {EnumStatus::Unknown, n, n};
            value = 10 + 2 * n;
          }
        } else if (!cardFont && n > 22)
          return {EnumStatus::Unknown, n, n};
      }
      if (!strcmp(key, "characterSpacing")) value = n - 5;
      if (!strcmp(key, "wordSpacing")) value = n <= 4 ? n : 4 - n;
      return {EnumStatus::Known, n, value};
    }
  return {EnumStatus::Unknown};
}
Status Settings::decode(SettingsInput input) {
  if (source_ >= Source::OldBinary || input.bytes > MaxJsonBytes) return Status::Unsupported;
  if (source_ >= Source::StorageLimit && source_ <= Source::IdleMinutes) {
    char text[32]{};
    if (input.bytes >= sizeof(text)) return Status::Unresolved;
    if (!input.read(input.context, 0, text, input.bytes)) return Status::IoError;
    static constexpr const char* storage[] = {"100", "250", "500", "1024", "2560", "5120", "10240", "0"};
    static constexpr const char* idle[] = {"1", "2", "5", "10"};
    static constexpr const char* wifi[] = {"always_on", "off_reading", "jit"};
    const auto list = source_ == Source::StorageLimit ? storage : source_ == Source::IdleMinutes ? idle : wifi;
    const size_t count = source_ == Source::StorageLimit  ? std::size(storage)
                         : source_ == Source::IdleMinutes ? std::size(idle)
                                                          : std::size(wifi);
    for (size_t i = 0; i < count; ++i)
      if (strlen(list[i]) == input.bytes && !memcmp(list[i], text, input.bytes)) {
        if (source_ == Source::WifiMode)
          option_ = static_cast<int32_t>(i);
        else {
          option_ = 0;
          for (size_t n = 0; n < input.bytes; ++n) option_ = option_ * 10 + text[n] - '0';
        }
        return Status::Ready;
      }
    return Status::Unresolved;
  }
  Reader reader{input};
  const auto error = deserializeJson(document_, reader, DeserializationOption::NestingLimit(12));
  if (reader.failed) return Status::IoError;
  if (error == DeserializationError::NoMemory || error == DeserializationError::TooDeep) return Status::Unsupported;
  if (error || !document_.is<JsonObject>()) return Status::Corrupt;
  for (int c = reader.read(); c >= 0; c = reader.read())
    if (!whitespace(c)) return Status::Corrupt;
  if (reader.failed) return Status::IoError;
  // These on-device JSON formats are unversioned. Never guess a future envelope.
  for (const auto* key : {"version", "schemaVersion", "schema"})
    if (!document_[key].isUnbound()) return Status::Unsupported;
  candidate_ = true;
  const auto begin = source_ == Source::Sleep ? sleepEnums : nativeEnums;
  const size_t count = source_ == Source::Sleep ? std::size(sleepEnums) : std::size(nativeEnums);
  for (size_t i = 0; i < count; ++i)
    if (enumValue(begin[i].key).status == EnumStatus::Unknown) return Status::Unresolved;
  return Status::Ready;
}
Status Settings::readFile(Source source, storage::PublishPaths rawBackup) {
  reset();
  source_ = source;
  const char* path = sourcePath(source);
  if (!path) return Status::Unsupported;
  if (!Storage.exists(path)) return Status::Missing;
  FsFile file = Storage.open(path, O_RDONLY);
  if (!file) {
    LOG_ERR("BSI", "Settings source open failed");
    return Status::IoError;
  }
  const auto input = fileInput(file);
  // fileBytes uses SIZE_MAX as a sentinel when a 32-bit HAL cannot represent
  // the full source length. Never fingerprint/copy a truncated length.
  auto result = input.bytes == SIZE_MAX ? Status::Unsupported : retain(input, rawBackup);
  if (result == Status::Ready) result = decode(input);
  if (!file.close()) result = Status::IoError;
  if (result != Status::Ready && result != Status::Unresolved) candidate_ = false;
  if (result != Status::Ready) LOG_ERR("BSI", "Settings source retained; import status=%u", unsigned(result));
  return result;
}
Status Settings::readPin(SettingsInput input, storage::PublishPaths rawBackup) {
  reset();
  if (!input.read) {
    LOG_ERR("BSI", "PIN input is unavailable");
    return Status::IoError;
  }
  auto result = retain(input, rawBackup);
  if (result == Status::Ready) {
    if (input.bytes != pin_.size())
      result = Status::Unsupported;
    else if (!input.read(input.context, 0, pin_.data(), pin_.size()))
      result = Status::IoError;
  }
  if (result != Status::Ready) {
    pin_.fill(0);
    LOG_ERR("BSI", "PIN source retained; import status=%u", unsigned(result));
  }
  return result;
}
Status Settings::readNvsPin(storage::PublishPaths rawBackup) {
  reset();
#if defined(ARDUINO_ARCH_ESP32) && !defined(SIMULATOR)
  nvs_handle_t handle;
  const auto opened = nvs_open("crossink_pin", NVS_READONLY, &handle);
  if (opened == ESP_ERR_NVS_NOT_FOUND) return Status::Missing;
  if (opened != ESP_OK) {
    LOG_ERR("BSI", "PIN namespace read failed");
    return Status::IoError;
  }
  size_t length = 0;
  const auto sized = nvs_get_blob(handle, "v1", nullptr, &length);
  // Unknown oversized records stay untouched in NVS and cannot authorize cutover.
  auto* raw = arena_.bytes;
  Status result = Status::Unsupported;
  if (sized == ESP_ERR_NVS_NOT_FOUND)
    result = Status::Missing;
  else if (sized != ESP_OK)
    result = Status::IoError;
  else if (length <= ArenaBytes) {
    if (nvs_get_blob(handle, "v1", raw, &length) == ESP_OK)
      result = readPin(SettingsInput::memory(raw, length), rawBackup);
    else
      result = Status::IoError;
  }
  nvs_close(handle);
  if (length <= ArenaBytes) memset(raw, 0, length);
  if (result != Status::Ready && result != Status::Missing)
    LOG_ERR("BSI", "PIN record retained; import status=%u", unsigned(result));
  return result;
#else
  (void)rawBackup;
  return Status::Unsupported;
#endif
}
}  // namespace booksync::import

// Dormant full-link gate only. No caller at boot; F29 will own activation.
extern "C" booksync::import::Status booksync_settings_import_link(booksync::import::Settings* workspace,
                                                                  booksync::import::Source source,
                                                                  booksync::storage::PublishPaths paths, bool pin) {
  return pin ? workspace->readNvsPin(paths) : workspace->readFile(source, paths);
}
