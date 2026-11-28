// SPDX-License-Identifier: GPL-3.0-or-later
#include "FilePublish.h"

#include <Logging.h>

#include <cstring>
#ifdef SIMULATOR
#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#endif

namespace booksync::storage {
#ifdef SIMULATOR
namespace {
uint32_t checkpointCount = 0;
}
uint32_t publicationCheckpoints() { return checkpointCount; }
#endif
namespace {
void checkpoint() {
#ifdef SIMULATOR
  // Test-only hard power loss, with no destructors/cleanup. Every publication
  // operation still uses the real simulator HAL and parent-directory barrier.
  if (std::getenv("BOOKSYNC_ACTIVATION_SCENARIO")) {
    const char* cut = std::getenv("BOOKSYNC_ACTIVATION_CUT");
    if (cut && std::strtoul(cut, nullptr, 10) == checkpointCount) {
      std::fflush(nullptr);
      std::_Exit(86);
    }
    ++checkpointCount;
  }
#endif
}
// The pinned native HAL has no volume-sync API. Flush its POSIX parent
// directory explicitly; hardware always uses the HAL volume cache barrier.
bool syncDirectory(const char* current) {
#ifdef SIMULATOR
  const char* root = std::getenv("CROSSPOINT_SIM_SD");
  if (!root || !*root) root = std::getenv("CROSSPOINT_EMU_SD");
  if (!root || !*root) root = "./fs_";
  const auto length = strrchr(current, '/') - current;
  char directory[256];
  if (length >= static_cast<decltype(length)>(sizeof(directory))) return false;
  const int count = snprintf(directory, sizeof(directory), "%s%.*s", root, static_cast<int>(length), current);
  if (count < 0 || static_cast<size_t>(count) >= sizeof(directory)) return false;
  const int fd = ::open(directory, O_RDONLY);
  if (fd < 0) return false;
  const bool synced = ::fsync(fd) == 0;
  const bool closed = ::close(fd) == 0;
  return synced && closed;
#else
  (void)current;
  return Storage.syncFilesystem();
#endif
}
bool suffixOf(const char* base, const char* path, const char* suffix) {
  if (!base || !path || base[0] != '/' || base[1] == '\0' || base[strlen(base) - 1] == '/') return false;
  if (strstr(base, "//") || strstr(base, "/./") || strstr(base, "/../")) return false;
  const char* last = strrchr(base, '/') + 1;
  if (strcmp(last, ".") == 0 || strcmp(last, "..") == 0) return false;
  const size_t length = strlen(base);
  return strlen(path) == length + strlen(suffix) && strncmp(base, path, length) == 0 &&
         strcmp(path + length, suffix) == 0;
}
bool validPaths(PublishPaths paths) {
  return suffixOf(paths.current, paths.staging, ".tmp") && suffixOf(paths.current, paths.backup, ".bak");
}
bool validate(const char* path, FileValidator validator, void* context) {
  checkpoint();
  FsFile file = Storage.open(path, O_RDONLY);
  checkpoint();
  if (!file) {
    LOG_ERR("BSP", "Publication candidate open failed");
    return false;
  }
  checkpoint();
  const bool valid = validator(file, context);
  checkpoint();
  const bool closed = file.close();
  checkpoint();
  if (!valid || !closed) LOG_ERR("BSP", "Publication candidate validation/close failed");
  return valid && closed;
}
}  // namespace

FilePublish::FilePublish(PublishPaths paths, FileValidator validator, void* context)
    : paths_(paths), validator_(validator), context_(context) {}
FilePublish::~FilePublish() {
  if (file_ && !file_.close()) LOG_ERR("BSP", "Publication abandoned close failed");
}
bool FilePublish::fail(const char* step) {
  LOG_ERR("BSP", "Publication failed: %s", step);
  failed_ = true;
  active_ = false;
  if (file_ && !file_.close()) LOG_ERR("BSP", "Publication failure close failed");
  return false;
}
PublishedFile FilePublish::select(PublishPaths paths, FileValidator validator, void* context) {
  if (!validPaths(paths) || !validator) {
    LOG_ERR("BSP", "Invalid publication paths/validator");
    return PublishedFile::Unreadable;
  }
  const bool current = Storage.exists(paths.current);
  if (current && validate(paths.current, validator, context)) return PublishedFile::Current;
  const bool backup = Storage.exists(paths.backup);
  if (backup && validate(paths.backup, validator, context)) return PublishedFile::Backup;
  return current || backup ? PublishedFile::Unreadable : PublishedFile::Missing;
}
bool FilePublish::begin(size_t expectedBytes) {
  if (active_) {
    LOG_ERR("BSP", "Publication already active");
    return false;
  }
  if (!validPaths(paths_) || !validator_) return fail("paths/validator");
  if (select(paths_, validator_, context_) == PublishedFile::Unreadable) return fail("unreadable source retained");
  checkpoint();
  file_ = Storage.open(paths_.staging, O_WRONLY | O_CREAT | O_TRUNC);
  checkpoint();
  if (!file_) return fail("staging open");
  remaining_ = expectedBytes;
  failed_ = false;
  active_ = true;
  return true;
}
bool FilePublish::resume(size_t expectedBytes, size_t received) {
  if (active_ || received > expectedBytes || !validPaths(paths_) || !validator_) return fail("resume bounds");
  // One handle at a time, including on the constrained SD transport.
  file_ = Storage.open(paths_.staging, O_RDONLY);
  if (!file_) return fail("resume open");
  const bool exact = file_.fileSize() == received;
  const bool closed = file_.close();
  if (!exact || !closed) return fail("resume length");
  file_ = Storage.open(paths_.staging, O_WRONLY);
  if (!file_ || !file_.seekSet(received)) return fail("resume seek");
  remaining_ = expectedBytes - received;
  failed_ = false;
  active_ = true;
  return true;
}
bool FilePublish::flush() {
  if (!active_ || failed_) return fail("inactive flush");
  if (!file_.sync() || !syncDirectory(paths_.current)) return fail("partial flush");
  return true;
}
bool FilePublish::checkpoint() {
  if (!active_ || failed_) return fail("inactive checkpoint");
  active_ = false;
  if (!file_.sync()) return fail("checkpoint sync");
  if (!file_.close()) return fail("checkpoint close");
  return syncDirectory(paths_.current) ? true : fail("checkpoint directory sync");
}
bool FilePublish::write(const void* bytes, size_t count) {
  if (!active_ || failed_) return fail("inactive write");
  if (count > remaining_ || (!bytes && count)) return fail("write bounds");
  checkpoint();
  if (count && file_.write(bytes, count) != count) return fail("short write");
  checkpoint();
  remaining_ -= count;
  return true;
}
bool FilePublish::validateCurrent(PublishPaths paths, FileValidator validator, void* context) {
  return validPaths(paths) && validator && validate(paths.current, validator, context);
}
bool FilePublish::finish() { return finishImpl(true); }
bool FilePublish::finishNew() { return finishImpl(false); }
bool FilePublish::finishImpl(bool replace) {
  if (!active_ || failed_ || remaining_) return fail("incomplete transaction");
  active_ = false;
  checkpoint();
  if (!file_.sync()) return fail("file sync");
  checkpoint();
  if (!file_.close()) return fail("file close");
  checkpoint();
  if (!syncDirectory(paths_.current)) return fail("staging directory sync");
  checkpoint();
  if (!validate(paths_.staging, validator_, context_)) return fail("staging validation");
  checkpoint();

  const auto source = select(paths_, validator_, context_);
  if (source == PublishedFile::Unreadable) return fail("source validation");
  if (!replace && source != PublishedFile::Missing) return fail("initial state already exists");
  if (source == PublishedFile::Current) {
    // Current remains valid while retiring the older backup. Never erase the
    // only valid source to make room for a rename.
    if (Storage.exists(paths_.backup)) {
      checkpoint();
      if (!Storage.remove(paths_.backup)) return fail("retire backup");
      checkpoint();
      if (!syncDirectory(paths_.current)) return fail("retire backup directory sync");
      checkpoint();
    }
    checkpoint();
    if (!Storage.rename(paths_.current, paths_.backup)) return fail("retain current");
    checkpoint();
    if (!syncDirectory(paths_.current)) return fail("retain current directory sync");
    checkpoint();
  } else if (source == PublishedFile::Backup && Storage.exists(paths_.current)) {
    checkpoint();
    if (!Storage.remove(paths_.current)) return fail("retire invalid current");
    checkpoint();
    if (!syncDirectory(paths_.current)) return fail("retire invalid directory sync");
    checkpoint();
  }
  checkpoint();
  if (!Storage.rename(paths_.staging, paths_.current)) return fail("publish rename");
  checkpoint();
  if (!syncDirectory(paths_.current)) return fail("publish directory sync");
  checkpoint();
  // Keep the backup. Read-only recovery needs no rename, cleanup or repair.
  return true;
}
}  // namespace booksync::storage
