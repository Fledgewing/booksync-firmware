// SPDX-License-Identifier: GPL-3.0-or-later
// CPV1 compatibility: CrossInk v2.0.0 Serialization/ObfuscationUtils.cpp.
#include "Credentials.h"

#include <ArduinoJson.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>

namespace booksync::legacy {
namespace {
constexpr size_t PoolBytes = 8192;
constexpr size_t HeaderBytes = 16;
ImportResult importFailure(ImportResult result) {
  LOG_ERR("BSI", "Credential import/restore deferred; status=%u", static_cast<unsigned>(result));
  return result;
}
uint32_t crc(const char* bytes, size_t size) {
  uint32_t value = ~uint32_t{0};
  for (size_t i = 0; i < size; ++i) {
    value ^= static_cast<uint8_t>(bytes[i]);
    for (unsigned bit = 0; bit < 8; ++bit) value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1u)));
  }
  return ~value;
}
uint32_t get32(const uint8_t* bytes) {
  return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) | (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
}
void put32(uint8_t* bytes, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) bytes[i] = value >> (8 * i);
}
std::string_view text(JsonVariantConst value) {
  const JsonString s = value.as<JsonString>();
  return s ? std::string_view(s.c_str(), s.size()) : std::string_view{};
}
bool bounded(JsonVariantConst value, size_t limit, bool allowEmpty = false) {
  return value.is<const char*>() && (allowEmpty || !text(value).empty()) && text(value).size() <= limit &&
         text(value).find('\0') == std::string_view::npos;
}
int base64Digit(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}
bool samePath(const char* a, const char* b) { return a && b && std::strcmp(a, b) == 0; }
bool privatePaths(const char* source, storage::PublishPaths paths) {
  if (!source || !paths.current || !paths.staging || !paths.backup) return false;
  if (source[0] != '/' || std::strstr(source, "..") || std::strstr(source, "//") || std::strstr(source, "/./"))
    return false;
  // FAT is case-insensitive: a case variation must not turn a private candidate
  // into its own source. Legacy inputs must be outside the owned directory.
  constexpr char prefix[] = "/.booksync/";
  bool owned = true;
  for (size_t i = 0; i < sizeof(prefix) - 1; ++i) {
    char c = source[i];
    if (!c) {
      owned = false;
      break;
    }
    if (c >= 'A' && c <= 'Z') c += ('a' - 'A');
    if (c != prefix[i]) {
      owned = false;
      break;
    }
  }
  if (owned) return false;
  // Only owned destinations; lexical aliases and legacy paths are rejected.
  for (const auto* path : {paths.current, paths.staging, paths.backup}) {
    if (std::strncmp(path, "/.booksync/", 11) != 0 || std::strstr(path, "..") || std::strstr(path, "//") ||
        samePath(source, path))
      return false;
  }
  return !samePath(paths.current, paths.staging) && !samePath(paths.current, paths.backup) &&
         !samePath(paths.staging, paths.backup);
}
}  // namespace
struct Credentials::Workspace final : ArduinoJson::Allocator {
  alignas(std::max_align_t) char pool[PoolBytes]{};
  size_t used = 0;
  char raw[MaxBytes + 1]{};
  char decoded[CredentialCapture::SecretCapacity + 8]{};
  size_t bytes = 0, decodedBytes = 0;
  CredentialCapture capture;
  JsonDocument doc{this};
  void* allocate(size_t size) override {
    if (size > PoolBytes) return nullptr;
    constexpr size_t alignment = alignof(std::max_align_t);
    size = (size + alignment - 1) & ~(alignment - 1);
    if (size > PoolBytes - used || alignment > PoolBytes - used - size) return nullptr;
    std::memcpy(pool + used, &size, sizeof(size));
    void* result = pool + used + alignment;
    used += size + alignment;
    return result;
  }
  void deallocate(void*) override {}
  void* reallocate(void* previous, size_t size) override {
    if (!previous) return allocate(size);
    size_t oldSize = 0;
    std::memcpy(&oldSize, static_cast<char*>(previous) - alignof(std::max_align_t), sizeof(oldSize));
    if (size <= oldSize) return previous;
    void* next = allocate(size);
    if (next) std::memcpy(next, previous, oldSize);
    return next;
  }
  void clear() {
    doc.clear();
    used = 0;
    bytes = decodedBytes = 0;
    capture.clear();
    eraseSecret(pool, sizeof(pool));
    eraseSecret(raw, sizeof(raw));
    eraseSecret(decoded, sizeof(decoded));
  }
  ~Workspace() { clear(); }
};
Credentials::Credentials(const uint8_t (&hardwareKey)[6]) : work_(makeUniqueNoThrow<Workspace>()) {
  static_assert(sizeof(Workspace) <= 20480);
  // One fallible ~19 KiB cold-job allocation: too large for a C3 worker stack;
  // static storage would retain private working bytes for the entire firmware life.
  std::memcpy(key_, hardwareKey, sizeof(key_));
  if (!work_)
    LOG_ERR("BSI", "Credential workspace allocation failed (%u bytes)", static_cast<unsigned>(sizeof(Workspace)));
}
Credentials::~Credentials() { eraseSecret(key_, sizeof(key_)); }
bool Credentials::loadSource(const char* path) {
  FsFile file = Storage.open(path, O_RDONLY);
  if (!file) {
    LOG_ERR("BSI", "Credential source open failed");
    return false;
  }
#if SIZE_MAX < UINT64_MAX
  const auto size = file.fileSize64();
#else
  const auto size = file.fileSize();
#endif
  bool valid = size > 0 && size <= MaxBytes;
  if (valid) {
    work_->bytes = static_cast<size_t>(size);
    valid = file.read(work_->raw, work_->bytes) == static_cast<int>(work_->bytes);
    work_->raw[work_->bytes] = 0;
  }
  const bool closed = file.close();
  if (!valid || !closed) LOG_ERR("BSI", "Credential source size/read/close failed");
  return valid && closed;
}
bool Credentials::decode(std::string_view encoded, size_t limit) {
  auto& w = *work_;
  eraseSecret(w.decoded, sizeof(w.decoded));
  w.decodedBytes = 0;
  if (encoded.empty() || encoded.size() % 4 || encoded.size() > ((limit + 10) / 3) * 4) return false;
  size_t count = 0;
  for (size_t i = 0; i < encoded.size(); i += 4) {
    uint32_t value = 0;
    unsigned padding = 0;
    for (unsigned j = 0; j < 4; ++j) {
      const char c = encoded[i + j];
      if (c == '=') {
        if (j < 2 || i + 4 != encoded.size()) return false;
        ++padding;
        value <<= 6;
      } else {
        const int digit = base64Digit(c);
        if (digit < 0 || padding) return false;
        value = (value << 6) | digit;
      }
    }
    if (padding > 2 || (padding == 1 && (value & 0xff)) || (padding == 2 && (value & 0xffff))) return false;
    for (unsigned j = 0; j < 3 - padding; ++j) {
      if (count >= sizeof(w.decoded)) return false;
      w.decoded[count] = static_cast<char>((value >> (16 - 8 * j)) ^ key_[count % 6]);
      ++count;
    }
  }
  if (count < 8 || std::memcmp(w.decoded, "CPV1", 4) != 0 || count - 8 > limit) return false;
  uint32_t hash = 2166136261u;
  for (uint8_t byte : key_) hash = (hash ^ byte) * 16777619u;
  for (size_t i = 8; i < count; ++i) hash = (hash ^ static_cast<uint8_t>(w.decoded[i])) * 16777619u;
  if (hash != get32(reinterpret_cast<const uint8_t*>(w.decoded + 4))) return false;
  w.decodedBytes = count - 8;
  std::memmove(w.decoded, w.decoded + 8, w.decodedBytes);
  return std::string_view(w.decoded, w.decodedBytes).find('\0') == std::string_view::npos;
}
bool Credentials::parse() {
  auto& w = *work_;
  w.doc.clear();
  w.used = 0;
  eraseSecret(w.pool, sizeof(w.pool));
  // Const reader keeps the source bytes exact for publication/proof. All copied
  // JSON strings live in the fixed, erased arena; no ArduinoJson heap growth.
  struct Reader {
    const char* data;
    size_t size, offset = 0;
    int read() { return offset < size ? static_cast<unsigned char>(data[offset++]) : -1; }
    size_t readBytes(char* out, size_t count) {
      count = std::min(count, size - offset);
      std::memcpy(out, data + offset, count);
      offset += count;
      return count;
    }
  } reader{w.raw, w.bytes};
  const auto error = deserializeJson(w.doc, reader, DeserializationOption::NestingLimit(6));
  if (error || !w.doc.is<JsonObject>()) return false;
  while (reader.offset < reader.size) {
    char c = reader.data[reader.offset++];
    if (c != ' ' && c != '\n' && c != '\r' && c != '\t') return false;
  }
  const auto root = w.doc.as<JsonObjectConst>();
  if (!root["version"].isNull() || !root["schemaVersion"].isNull()) return false;
  if (family_ == CredentialFamily::NativeSync) {
    if (!root["cfgVersion"].is<int>() || root["cfgVersion"].as<int>() != 3 || !root["serverType"].is<unsigned>() ||
        root["serverType"].as<unsigned>() > 2 || (!root["headers"].isNull() && !root["headers"].is<JsonArrayConst>()) ||
        root["headers"].size() > 2)
      return false;
  } else if (!root["cfgVersion"].isNull())
    return false;
  auto login = [&](JsonVariantConst row, const char* origin) {
    return row["password"].isNull() && bounded(row[origin], CredentialCapture::OriginCapacity, true) &&
           bounded(row["username"], CredentialCapture::UserCapacity) && bounded(row["password_obf"], 1400) &&
           decode(text(row["password_obf"]), CredentialCapture::SecretCapacity);
  };
  if (family_ == CredentialFamily::Wifi) {
    if (!root["credentials"].is<JsonArrayConst>() || !bounded(root["lastConnectedSsid"], 32, true)) return false;
    const auto rows = root["credentials"].as<JsonArrayConst>();
    if (rows.size() > 8) return false;
    for (size_t i = 0; i < rows.size(); ++i) {
      const auto row = rows[i];
      if (!row["password"].isNull() || !bounded(row["ssid"], 32) || !bounded(row["password_obf"], 128, true))
        return false;
      for (size_t j = 0; j < i; ++j)
        if (text(rows[j]["ssid"]) == text(row["ssid"])) return false;
      const auto password = text(row["password_obf"]);
      if (!password.empty() && !decode(password, 64)) return false;
    }
    return true;
  }
  if (family_ == CredentialFamily::Acquisition) {
    if (!root["servers"].is<JsonArrayConst>() || root["servers"].size() > 8 || !root["servers"].size()) return false;
    for (JsonVariantConst row : root["servers"].as<JsonArrayConst>())
      if (!login(row, "url")) return false;
    return true;
  }
  if (family_ != CredentialFamily::LibrarySync && family_ != CredentialFamily::NativeSync) return false;
  if (!login(root, "serverUrl")) return false;
  if (!root["cfClientSecret_obf"].isNull() &&
      (!bounded(root["cfClientId"], 256) || !decode(text(root["cfClientSecret_obf"]), 1024)))
    return false;
  return true;
}
bool Credentials::readSnapshot(FsFile& file) {
  uint8_t header[HeaderBytes]{};
  if (file.read(header, sizeof(header)) != sizeof(header) || std::memcmp(header, "BSI1", 4) != 0 ||
      header[4] != static_cast<uint8_t>(family_) || header[5] || header[6] || header[7])
    return false;
  const size_t size = get32(header + 8);
#if SIZE_MAX < UINT64_MAX
  const auto total = file.fileSize64();
#else
  const auto total = file.fileSize();
#endif
  if (!size || size > MaxBytes || total != size + HeaderBytes) return false;
  work_->bytes = size;
  if (file.read(work_->raw, size) != static_cast<int>(size) || crc(work_->raw, size) != get32(header + 12))
    return false;
  work_->raw[size] = 0;
  return parse();
}
bool Credentials::validateSnapshot(FsFile& file, void* context) {
  return static_cast<Credentials*>(context)->readSnapshot(file);
}
bool Credentials::loadSnapshot(storage::PublishPaths paths) {
  const auto selected = storage::FilePublish::select(paths, validateSnapshot, this);
  const char* path = selected == storage::PublishedFile::Current  ? paths.current
                     : selected == storage::PublishedFile::Backup ? paths.backup
                                                                  : nullptr;
  if (!path) return false;
  FsFile file = Storage.open(path, O_RDONLY);
  if (!file) return false;
  const bool valid = readSnapshot(file);
  const bool closed = file.close();
  return valid && closed;
}
ImportResult Credentials::stage(CredentialFamily family, const char* source, storage::PublishPaths paths) {
  if (!work_) return importFailure(ImportResult::IoError);
  struct Cleanup {
    Workspace& w;
    ~Cleanup() { w.clear(); }
  } cleanup{*work_};
  family_ = family;
  if (!privatePaths(source, paths)) return importFailure(ImportResult::Unresolved);
  if (!loadSource(source)) return importFailure(ImportResult::IoError);
  if (!parse()) {
    LOG_ERR("BSI", "Credential source unresolved; retained");
    return importFailure(ImportResult::Unresolved);
  }
  // A copy is its own exact-byte source fingerprint/proof. Never overwrite any
  // previous owned source, including unreadable/unknown snapshots or backups.
  if (Storage.exists(paths.current) || Storage.exists(paths.backup)) {
    const auto selected = storage::FilePublish::select(paths, validateSnapshot, this);
    const char* path = selected == storage::PublishedFile::Current  ? paths.current
                       : selected == storage::PublishedFile::Backup ? paths.backup
                                                                    : nullptr;
    if (!path) return importFailure(ImportResult::Conflict);
    if (!loadSource(source) || !parse()) return importFailure(ImportResult::Unresolved);
    FsFile file = Storage.open(path, O_RDONLY);
    if (!file) return importFailure(ImportResult::IoError);
    bool same = file.seekSet(HeaderBytes);
    char chunk[128];
    for (size_t offset = 0; same && offset < work_->bytes; offset += sizeof(chunk)) {
      const size_t count = std::min(sizeof(chunk), work_->bytes - offset);
      same = file.read(chunk, count) == static_cast<int>(count) && std::memcmp(chunk, work_->raw + offset, count) == 0;
    }
    same = same && file.fileSize() == work_->bytes + HeaderBytes;
    eraseSecret(chunk, sizeof(chunk));
    const bool closed = file.close();
    if (!closed) return importFailure(ImportResult::IoError);
    return same ? ImportResult::AlreadyImported : importFailure(ImportResult::Conflict);
  }
  uint8_t header[HeaderBytes]{'B', 'S', 'I', '1', static_cast<uint8_t>(family)};
  put32(header + 8, work_->bytes);
  put32(header + 12, crc(work_->raw, work_->bytes));
  storage::FilePublish writer(paths, validateSnapshot, this);
  if (!writer.begin(HeaderBytes + work_->bytes) || !writer.write(header, sizeof(header)) ||
      !writer.write(work_->raw, work_->bytes) || !writer.finishNew())
    return importFailure(ImportResult::IoError);
  return ImportResult::Imported;
}
ImportResult Credentials::restore(CredentialFamily family, storage::PublishPaths paths, Account& account,
                                  size_t index) {
  if (!work_) return importFailure(ImportResult::IoError);
  struct Cleanup {
    Workspace& w;
    ~Cleanup() { w.clear(); }
  } cleanup{*work_};
  family_ = family;
  if (family == CredentialFamily::Wifi || !loadSnapshot(paths)) return importFailure(ImportResult::Unresolved);
  JsonVariantConst row = work_->doc.as<JsonVariantConst>();
  // Custom legacy header authorization is retained in the private proof. F15
  // has no arbitrary-header restore capability; never drop it during activation.
  if (family == CredentialFamily::NativeSync) {
    for (JsonVariantConst header : row["headers"].as<JsonArrayConst>()) {
      // The legacy serializer emits two empty slots even with no custom auth.
      if (!header.is<JsonObjectConst>() || !bounded(header["name"], 256, true) ||
          !bounded(header["value_obf"], 1400, true) || !text(header["name"]).empty() ||
          !text(header["value_obf"]).empty() || !header["value"].isNull())
        return importFailure(ImportResult::Unresolved);
    }
  }
  const bool acquisition = family == CredentialFamily::Acquisition;
  if (acquisition) {
    if (index >= row["servers"].size()) return importFailure(ImportResult::Unresolved);
    row = row["servers"][index];
  } else if (index)
    return importFailure(ImportResult::Unresolved);
  const auto origin = text(row[acquisition ? "url" : "serverUrl"]);
  if (origin.empty()) return importFailure(ImportResult::NeedsOrigin);
  if (!decode(text(row["password_obf"]), CredentialCapture::SecretCapacity))
    return importFailure(ImportResult::Unresolved);
  const auto kind = acquisition ? LoginKind::Acquisition : LoginKind::Sync;
  if (!work_->capture.capture(kind, origin, text(row["username"]), work_->decoded, work_->decodedBytes))
    return importFailure(ImportResult::Unresolved);
  LoginAuthorization authorization{};
  if (!text(row["cfClientId"]).empty() || !text(row["cfClientSecret_obf"]).empty()) {
    if (!decode(text(row["cfClientSecret_obf"]), CredentialCapture::SecretCapacity))
      return importFailure(ImportResult::Unresolved);
    authorization = {text(row["cfClientId"]), {work_->decoded, work_->decodedBytes}};
  }
  const auto error = account.restoreLogin(work_->capture, account.snapshot().ticket, authorization);
  return error == AuthError::None ? ImportResult::Restored : importFailure(ImportResult::Unresolved);
}
bool Credentials::proof(CredentialFamily family, storage::PublishPaths paths, uint32_t& value) {
  if (!work_) return false;
  family_ = family;
  const bool valid = loadSnapshot(paths);
  if (valid) value = crc(work_->raw, work_->bytes) ^ (static_cast<uint32_t>(family) << 28);
  work_->clear();
  return valid;
}
size_t Credentials::loginCount(storage::PublishPaths paths) {
  if (!work_) return 0;
  family_ = CredentialFamily::Acquisition;
  const size_t count = loadSnapshot(paths) ? work_->doc["servers"].size() : 0;
  work_->clear();
  return count;
}
bool Credentials::activateWifi(storage::PublishPaths paths) {
  if (!work_) return false;
  family_ = CredentialFamily::Wifi;
  constexpr storage::PublishPaths active{"/.booksync/wifi.json", "/.booksync/wifi.json.tmp",
                                         "/.booksync/wifi.json.bak"};
  auto validate = [](FsFile& file, void* context) {
    auto& importer = *static_cast<Credentials*>(context);
    auto& w = *importer.work_;
#if SIZE_MAX < UINT64_MAX
    const auto bytes = file.fileSize64();
#else
    const auto bytes = file.fileSize();
#endif
    if (!bytes || bytes > MaxBytes) return false;
    w.bytes = static_cast<size_t>(bytes);
    return file.read(w.raw, w.bytes) == static_cast<int>(w.bytes) && importer.parse();
  };
  const auto selected = storage::FilePublish::select(active, validate, this);
  if (selected != storage::PublishedFile::Missing) {
    work_->clear();
    return selected == storage::PublishedFile::Current;
  }
  if (!loadSnapshot(paths)) {
    work_->clear();
    return false;
  }
  storage::FilePublish writer(active, validate, this);
  const bool ok = writer.begin(work_->bytes) && writer.write(work_->raw, work_->bytes) && writer.finishNew();
  work_->clear();
  return ok;
}
ImportResult Credentials::restoreWifi(storage::PublishPaths paths, WifiTarget& target) {
  if (!work_) return importFailure(ImportResult::IoError);
  struct Cleanup {
    Workspace& w;
    ~Cleanup() { w.clear(); }
  } cleanup{*work_};
  family_ = CredentialFamily::Wifi;
  if (!loadSnapshot(paths)) return importFailure(ImportResult::Unresolved);
  auto rows = work_->doc["credentials"].as<JsonArrayConst>();
  bool accepted = target.begin(rows.size(), text(work_->doc["lastConnectedSsid"]));
  for (JsonVariantConst row : rows) {
    if (!accepted) break;
    const auto encoded = text(row["password_obf"]);
    work_->decodedBytes = 0;
    accepted = (encoded.empty() || decode(encoded, 64)) &&
               target.add(text(row["ssid"]), {work_->decoded, work_->decodedBytes});
    eraseSecret(work_->decoded, sizeof(work_->decoded));
    work_->decodedBytes = 0;
  }
  const bool committed = target.finish(accepted);
  return accepted && committed ? ImportResult::Restored : importFailure(ImportResult::Unresolved);
}
}  // namespace booksync::legacy

// Dormant early full-link gate. F29 will own admission/activation; this entry is
// retained for honest device size measurements and is never called at boot.
extern "C" __attribute__((used, retain)) booksync::legacy::ImportResult booksync_credentials_import_link(
    const uint8_t (&key)[6], booksync::legacy::CredentialFamily family, const char* source,
    booksync::storage::PublishPaths paths, booksync::Account& account, booksync::legacy::WifiTarget& wifi) {
  booksync::legacy::Credentials importer(key);
  const auto result = importer.stage(family, source, paths);
  if (result != booksync::legacy::ImportResult::Imported && result != booksync::legacy::ImportResult::AlreadyImported)
    return result;
  return family == booksync::legacy::CredentialFamily::Wifi ? importer.restoreWifi(paths, wifi)
                                                            : importer.restore(family, paths, account);
}
