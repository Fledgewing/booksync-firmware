// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <ArduinoJson.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "booksync/storage/FilePublish.h"

namespace booksync::import {
// Worker-only candidates. None of these APIs applies settings, unlocks a PIN,
// writes NVS, retires a legacy file, or activates a writer. F29 owns activation.
enum class Source : uint8_t {
  Native,
  NativeBackup,
  OldNative,
  Sleep,
  StorageLimit,
  WifiMode,
  IdleMinutes,
  OldBinary,
  OldBinaryBackup,
  Language,
  LanguageBackup
};
enum class Status : uint8_t { Missing, Ready, Unresolved, Corrupt, Unsupported, IoError, BackupConflict };
enum class EnumStatus : uint8_t { Missing, Known, Unknown };
struct EnumValue {
  EnumStatus status = EnumStatus::Missing;
  int32_t raw = 0;
  int32_t value = 0;
};
struct SettingsInput {
  using Read = bool (*)(void*, size_t, void*, size_t);
  Read read = nullptr;
  void* context = nullptr;
  size_t bytes = 0;
  static SettingsInput memory(const void* data, size_t bytes);
};
class Settings {
 public:
  static constexpr uint8_t ProofVersion = 1;
  static constexpr size_t MaxJsonBytes = 16384;
  static constexpr size_t ArenaBytes = 24576;
  Settings() : document_(&arena_) {}
  Settings(const Settings&) = delete;
  Settings& operator=(const Settings&) = delete;
  // ~25 KiB reusable workspace: keep off a task stack. No payload heap allocations
  // or PSRAM assumption. F29 can own one fallibly allocated worker workspace.
  Status readFile(Source source, storage::PublishPaths rawBackup);
  Status readPin(SettingsInput blob, storage::PublishPaths rawBackup);
  Status readNvsPin(storage::PublishPaths rawBackup);
  // Candidates are borrowed until the next read/destruction. Raw JSON keys,
  // types and unknown fields are preserved; service mappings belong to F38.
  JsonVariantConst values() const { return candidate_ ? document_.as<JsonVariantConst>() : JsonVariantConst{}; }
  EnumValue enumValue(const char* key) const;
  int32_t optionValue() const { return option_; }              // only meaningful after Ready for a plain-option source
  const std::array<uint8_t, 48>& pin() const { return pin_; }  // only after Ready from a PIN source
  // Borrowed raw-backup path; caller paths must outlive this candidate.
  const std::array<uint8_t, 32>& fingerprint() const { return fingerprint_; }
  size_t sourceBytes() const { return sourceBytes_; }
  const char* retainedPath() const { return retained_; }
  size_t arenaPeak() const { return arena_.used; }
  static const char* sourcePath(Source source);

 private:
  struct Arena final : ArduinoJson::Allocator {
    alignas(std::max_align_t) uint8_t bytes[ArenaBytes]{};
    size_t used = 0;
    void* allocate(size_t size) override;
    void deallocate(void*) override {}
    void* reallocate(void* pointer, size_t size) override;
  } arena_;
  JsonDocument document_;
  std::array<uint8_t, 48> pin_{};
  std::array<uint8_t, 32> fingerprint_{};
  size_t sourceBytes_ = 0;
  const char* retained_ = nullptr;
  Source source_ = Source::Native;
  bool candidate_ = false;
  int32_t option_ = 0;
  void reset();
  Status decode(SettingsInput input);
  Status retain(SettingsInput input, storage::PublishPaths paths);
};
}  // namespace booksync::import
