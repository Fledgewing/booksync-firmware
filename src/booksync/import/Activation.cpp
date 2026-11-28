// SPDX-License-Identifier: GPL-3.0-or-later
#include "Activation.h"

#include <Logging.h>

#include <cstdio>
#include <cstring>

namespace booksync::legacy {
namespace {
constexpr storage::PublishPaths recordPaths{"/.booksync/import/activation", "/.booksync/import/activation.tmp",
                                            "/.booksync/import/activation.bak"};
constexpr storage::PublishPaths snapshots[] = {
    {"/.booksync/import/sync", "/.booksync/import/sync.tmp", "/.booksync/import/sync.bak"},
    {"/.booksync/import/native", "/.booksync/import/native.tmp", "/.booksync/import/native.bak"},
    {"/.booksync/import/acquisition", "/.booksync/import/acquisition.tmp", "/.booksync/import/acquisition.bak"},
    {"/.booksync/import/networks", "/.booksync/import/networks.tmp", "/.booksync/import/networks.bak"},
};
constexpr const char* sources[] = {"/.crosspoint/bookorbit.json", "/.crosspoint/koreader.json",
                                   "/.crosspoint/opds.json", "/.crosspoint/wifi.json"};
constexpr storage::PublishPaths noticePaths{"/.booksync/import/notice-done", "/.booksync/import/notice-done.tmp",
                                            "/.booksync/import/notice-done.bak"};
uint64_t storedSize(FsFile& file) {
#if SIZE_MAX < UINT64_MAX
  return file.fileSize64();
#else
  return file.fileSize();
#endif
}
uint32_t checksum(const uint8_t* bytes, size_t size) {
  uint32_t value = 2166136261u;
  while (size--) value = (value ^ *bytes++) * 16777619u;
  return value;
}
void put32(uint8_t* bytes, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) bytes[i] = value >> (8 * i);
}
uint32_t get32(const uint8_t* bytes) {
  return uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 | uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
}
bool exists(storage::PublishPaths paths) {
  return Storage.exists(paths.current) || Storage.exists(paths.staging) || Storage.exists(paths.backup);
}
bool validMarker(FsFile& file, void*) {
  uint8_t bytes[8]{};
  return storedSize(file) == sizeof(bytes) && file.read(bytes, sizeof(bytes)) == sizeof(bytes) &&
         std::memcmp(bytes, "BSR1", 4) == 0 && get32(bytes + 4) == checksum(bytes, 4);
}
bool mark(storage::PublishPaths paths) {
  // An acknowledged marker is never rewritten; partial markers remain a
  // conservative denial of restore/presentation until this command is retried.
  const auto selected = storage::FilePublish::select(paths, validMarker);
  if (selected == storage::PublishedFile::Current || selected == storage::PublishedFile::Backup) return true;
  if (selected == storage::PublishedFile::Unreadable) {
    LOG_ERR("BSA", "Retirement record unreadable; retained and treated as retired");
    return false;
  }
  uint8_t bytes[8]{'B', 'S', 'R', '1'};
  put32(bytes + 4, checksum(bytes, 4));
  storage::FilePublish writer(paths, validMarker);
  return writer.begin(sizeof(bytes)) && writer.write(bytes, sizeof(bytes)) && writer.finish();
}
bool legacyAlternate(unsigned family) {
  char path[64];
  for (const char* suffix : {".bak", ".tmp"}) {
    std::snprintf(path, sizeof(path), "%s%s", sources[family], suffix);
    if (Storage.exists(path)) return true;
  }
  // Older native stores used binary layouts. Preserve and report them, never
  // coerce them into current credentials or recover an old login implicitly.
  for (const char* suffix : {".bin", ".bin.bak", ".bin.tmp"}) {
    std::snprintf(path, sizeof(path), "%.*s%s", static_cast<int>(std::strlen(sources[family]) - 5), sources[family],
                  suffix);
    if (Storage.exists(path)) return true;
  }
  return false;
}
bool slot(CredentialFamily family, size_t index, unsigned& id) {
  if (family == CredentialFamily::LibrarySync || family == CredentialFamily::NativeSync) {
    id = static_cast<unsigned>(family);
    return index == 0;
  }
  if (family == CredentialFamily::Acquisition && index < 8) {
    id = 2 + index;
    return true;
  }
  return false;
}
struct MarkerPaths {
  char current[40], staging[44], backup[44];
  explicit MarkerPaths(unsigned id, const char* stem = "removed") {
    std::snprintf(current, sizeof(current), "/.booksync/import/%s-%u", stem, id);
    std::snprintf(staging, sizeof(staging), "%s.tmp", current);
    std::snprintf(backup, sizeof(backup), "%s.bak", current);
  }
  storage::PublishPaths paths() const { return {current, staging, backup}; }
};
static_assert(sizeof(MarkerPaths) <= 256);
enum class ScopeState : uint8_t { Retired = 0, Live = 1, Unknown = 2 };
struct ScopeValidation {
  unsigned id;
  ScopeState value = ScopeState::Unknown;
};
bool validScope(FsFile& file, void* context) {
  auto& wanted = *static_cast<ScopeValidation*>(context);
  uint8_t bytes[12]{};
  if (storedSize(file) != sizeof(bytes) || file.read(bytes, sizeof(bytes)) != sizeof(bytes) ||
      std::memcmp(bytes, "BSL1", 4) || bytes[4] != wanted.id || bytes[5] > 1 || bytes[6] || bytes[7] ||
      get32(bytes + 8) != checksum(bytes, 8))
    return false;
  wanted.value = static_cast<ScopeState>(bytes[5]);
  return true;
}
ScopeState scopeState(unsigned id) {
  const MarkerPaths paths(id, "state");
  ScopeValidation validation{id};
  return storage::FilePublish::validateCurrent(paths.paths(), validScope, &validation) ? validation.value
                                                                                       : ScopeState::Unknown;
}
bool writeScope(unsigned id, ScopeState state, bool initial = false) {
  const auto previous = scopeState(id);
  if (initial && previous != ScopeState::Unknown) return true;  // Retired stays retired.
  if (!initial && previous == state) return true;
  const MarkerPaths paths(id, "state");
  ScopeValidation validation{id};
  uint8_t bytes[12]{'B', 'S', 'L', '1', static_cast<uint8_t>(id), static_cast<uint8_t>(state)};
  put32(bytes + 8, checksum(bytes, 8));
  storage::FilePublish writer(paths.paths(), validScope, &validation);
  return writer.begin(sizeof(bytes)) && writer.write(bytes, sizeof(bytes)) &&
         (initial ? writer.finishNew() : writer.finish());
}
bool retireScopeMarker(unsigned id) {
  const MarkerPaths paths(id);
  return mark(paths.paths());
}
bool committedRetirement(unsigned id) {
  const auto complete = [](storage::PublishPaths paths) {
    const auto selected = storage::FilePublish::select(paths, validMarker);
    return selected == storage::PublishedFile::Current || selected == storage::PublishedFile::Backup;
  };
  if (id == 10) return complete(noticePaths);
  const MarkerPaths paths(id);
  return complete(paths.paths());
}
ScopeState recoverScope(unsigned id) {
  const auto state = scopeState(id);
  if (state != ScopeState::Retired && committedRetirement(id)) {
    // A complete removal receipt authorizes only closing the slot. It cannot
    // promote a former Live backup or repair a credential/legacy source.
    return writeScope(id, ScopeState::Retired) ? ScopeState::Retired : ScopeState::Unknown;
  }
  return state;
}
}  // namespace
Activation::Activation() {
  for (unsigned i = 0; i < 10; ++i)
    retirements_[i] = {this, i < 2 ? static_cast<CredentialFamily>(i) : CredentialFamily::Acquisition,
                       i < 2 ? 0 : i - 2};
}
bool Activation::validRecord(FsFile& file, void* context) {
  uint8_t bytes[64]{};
  if (storedSize(file) != sizeof(bytes) || file.read(bytes, sizeof(bytes)) != sizeof(bytes) ||
      std::memcmp(bytes, "BSA1", 4) || bytes[4] > 1 || bytes[5] > 15 || bytes[6] > 15 || bytes[56] > 8 ||
      bytes[7] > static_cast<unsigned>(import::IdentityStatus::ReadError) || get32(bytes + 60) != checksum(bytes, 60))
    return false;
  for (unsigned i = 57; i < 60; ++i)
    if (bytes[i]) return false;
  if (context) std::memcpy(context, bytes, sizeof(bytes));
  return true;
}
bool Activation::readerStatePresent() { return Storage.exists("/.crosspoint"); }
ActivationResult Activation::boot(Credentials& credentials, const import::IdentityAudit& identity,
                                  bool priorReaderState) {
  ready_ = identityReady_ = stateRecovery_ = false;
  auto fail = [] {
    LOG_ERR("BSA", "Upgrade activation unavailable; sources retained and saved reading available");
    return ActivationResult::Unavailable;
  };
  if ((!Storage.exists("/.booksync") && !Storage.mkdir("/.booksync")) ||
      (!Storage.exists("/.booksync/import") && !Storage.mkdir("/.booksync/import")))
    return fail();
  const auto selected = storage::FilePublish::select(recordPaths, validRecord, record_);
  if (selected == storage::PublishedFile::Unreadable) return fail();
  if (selected == storage::PublishedFile::Missing) {
    // A transient NVS read/init failure is not a frozen identity decision.
    // Retry the audit on the next boot; never activate from an unreadable store.
    if (identity.status == import::IdentityStatus::ReadError) return fail();
    std::memset(record_, 0, sizeof(record_));
    std::memcpy(record_, "BSA1", 4);
    record_[4] = priorReaderState || identity.status != import::IdentityStatus::Absent;
    record_[7] = static_cast<uint8_t>(identity.status);
    std::memcpy(record_ + 8, identity.fingerprint.data(), 32);
    for (unsigned i = 0; i < 4; ++i) {
      if (!Storage.exists(sources[i])) {
        if (exists(snapshots[i])) return fail();  // Do not abandon a previously staged source.
        if (legacyAlternate(i)) {
          record_[4] = 1;
          record_[6] |= 1u << i;
        }
        continue;
      }
      record_[4] = 1;
      const auto family = static_cast<CredentialFamily>(i);
      const auto result = credentials.stage(family, sources[i], snapshots[i]);
      if (result == ImportResult::IoError || result == ImportResult::Conflict) return fail();
      if (result == ImportResult::Imported || result == ImportResult::AlreadyImported) {
        uint32_t proof = 0;
        if (!credentials.proof(family, snapshots[i], proof)) return fail();
        if (family == CredentialFamily::Acquisition) record_[56] = credentials.loginCount(snapshots[i]);
        record_[5] |= 1u << i;
        put32(record_ + 40 + 4 * i, proof);
        if (family == CredentialFamily::Wifi && !credentials.activateWifi(snapshots[i])) return fail();
      } else
        record_[6] |= 1u << i;  // Unresolved originals are retained; never guessed.
    }
    // Positive per-scope state is committed before the one activation cutover.
    // Missing/damaged state is never interpreted as a live imported credential.
    for (unsigned family = 0; family < 3; ++family) {
      if (!(record_[5] & (1u << family))) continue;
      const unsigned count = family == 2 ? record_[56] : 1;
      for (unsigned index = 0; index < count; ++index) {
        const unsigned id = family == 2 ? index + 2 : family;
        if (!writeScope(id, ScopeState::Live, true)) return fail();
      }
    }
    if (record_[4] && !writeScope(10, ScopeState::Live, true)) return fail();
    put32(record_ + 60, checksum(record_, 60));
    storage::FilePublish writer(recordPaths, validRecord);
    if (!writer.begin(sizeof(record_)) || !writer.write(record_, sizeof(record_)) || !writer.finishNew()) return fail();
  }
  // Frozen proof is the only restore source after cutover. Never inspect or
  // replay legacy files on an ordinary boot, even if they later change.
  for (unsigned i = 0; i < 4; ++i) {
    if (!(record_[5] & (1u << i))) continue;
    uint32_t proof = 0;
    if (!credentials.proof(static_cast<CredentialFamily>(i), snapshots[i], proof) ||
        proof != get32(record_ + 40 + 4 * i))
      return fail();
  }
  for (unsigned family = 0; family < 3; ++family) {
    if (!(record_[5] & (1u << family))) continue;
    const unsigned count = family == 2 ? record_[56] : 1;
    for (unsigned index = 0; index < count; ++index)
      if (recoverScope(family == 2 ? index + 2 : family) == ScopeState::Unknown) stateRecovery_ = true;
  }
  if (record_[4] && recoverScope(10) == ScopeState::Unknown) stateRecovery_ = true;
  ready_ = true;
  identityReady_ = identity.status == import::IdentityStatus::RetainedInPlace &&
                   record_[7] == static_cast<unsigned>(import::IdentityStatus::RetainedInPlace) &&
                   std::memcmp(record_ + 8, identity.fingerprint.data(), 32) == 0;
  return ActivationResult::Active;
}
bool Activation::removed(CredentialFamily family, size_t index) const {
  unsigned id = 0;
  if (!slot(family, index, id)) return true;
  const MarkerPaths marker(id);
  return exists(marker.paths());
}
bool Activation::hasLogin(CredentialFamily family, size_t index) const {
  unsigned id = 0;
  return ready_ && slot(family, index, id) && (record_[5] & (1u << static_cast<unsigned>(family))) &&
         !removed(family, index) && scopeState(id) == ScopeState::Live;
}
bool Activation::retire(LoginKind, void* context) {
  const auto& value = *static_cast<Retirement*>(context);
  return value.owner->remove(value.family, value.index);
}
ImportResult Activation::restore(Credentials& credentials, CredentialFamily family, Account& account, size_t index) {
  unsigned id = 0;
  if (!hasLogin(family, index) || !slot(family, index, id)) return ImportResult::Unresolved;
  const auto result = credentials.restore(family, snapshots[static_cast<unsigned>(family)], account, index);
  if (result == ImportResult::Restored)
    account.setImportedLoginRetirer(family == CredentialFamily::Acquisition ? LoginKind::Acquisition : LoginKind::Sync,
                                    retire, &retirements_[id]);
  return result;
}
bool Activation::remove(CredentialFamily family, size_t index) {
  unsigned id = 0;
  if (!ready_ || !slot(family, index, id)) return false;
  return retireScopeMarker(id) && writeScope(id, ScopeState::Retired);
}
bool Activation::noticePending() const {
  return ready_ && record_[4] && scopeState(10) == ScopeState::Live && !exists(noticePaths);
}
bool Activation::dismissNotice() { return ready_ && mark(noticePaths) && writeScope(10, ScopeState::Retired); }
}  // namespace booksync::legacy
