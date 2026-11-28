// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <memory>

#include "booksync/services/Account.h"
#include "booksync/storage/FilePublish.h"

namespace booksync::legacy {
enum class CredentialFamily : uint8_t { LibrarySync, NativeSync, Acquisition, Wifi };
enum class ImportResult : uint8_t { Imported, AlreadyImported, Restored, NeedsOrigin, Unresolved, Conflict, IoError };
// Worker-private transaction. begin/add stage candidates; finish(false) erases
// them and preserves the previous saved networks. No radio or presentation here.
class WifiTarget {
 public:
  virtual ~WifiTarget() = default;
  virtual bool begin(size_t count, std::string_view lastConnected) = 0;
  virtual bool add(std::string_view ssid, std::string_view password) = 0;
  virtual bool finish(bool accept) = 0;
};
// F26b stages private, versioned, exact-byte copies; F29 owns activation and
// supplies disjoint destination paths below its private directory. Never pass a
// legacy source/backup as a destination. No boot hook, network or source writes.
// Allocate once per import job; ~20 KiB bounded workspace is fallible, erased on
// every return, reused across families, and released with the job (safe on C3).
class Credentials final {
 public:
  static constexpr size_t MaxBytes = 8192;
  explicit Credentials(const uint8_t (&hardwareKey)[6]);
  ~Credentials();
  Credentials(const Credentials&) = delete;
  Credentials& operator=(const Credentials&) = delete;
  ImportResult stage(CredentialFamily family, const char* source, storage::PublishPaths destination);
  ImportResult restore(CredentialFamily family, storage::PublishPaths destination, Account& account, size_t index = 0);
  // Private cutover proof and owned Wi-Fi publication; never read a legacy
  // source here. Proof binds the complete validated snapshot, including family.
  bool proof(CredentialFamily family, storage::PublishPaths paths, uint32_t& value);
  size_t loginCount(storage::PublishPaths paths);
  bool activateWifi(storage::PublishPaths paths);
  ImportResult restoreWifi(storage::PublishPaths destination, WifiTarget& target);

 private:
  struct Workspace;
  std::unique_ptr<Workspace> work_;
  uint8_t key_[6]{};
  CredentialFamily family_{};
  bool loadSource(const char* path);
  bool parse();
  bool decode(std::string_view encoded, size_t limit);
  bool readSnapshot(FsFile& file);
  bool loadSnapshot(storage::PublishPaths paths);
  static bool validateSnapshot(FsFile& file, void* context);
};
}  // namespace booksync::legacy
