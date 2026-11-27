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
namespace {
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
  FsFile file = Storage.open(path, O_RDONLY);
  if (!file) {
    LOG_ERR("BSP", "Publication candidate open failed");
    return false;
  }
  const bool valid = validator(file, context);
  const bool closed = file.close();
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
  file_ = Storage.open(paths_.staging, O_WRONLY | O_CREAT | O_TRUNC);
  if (!file_) return fail("staging open");
  remaining_ = expectedBytes;
  failed_ = false;
  active_ = true;
  return true;
}
bool FilePublish::write(const void* bytes, size_t count) {
  if (!active_ || failed_) return fail("inactive write");
  if (count > remaining_ || (!bytes && count)) return fail("write bounds");
  if (count && file_.write(bytes, count) != count) return fail("short write");
  remaining_ -= count;
  return true;
}
bool FilePublish::finish() {
  if (!active_ || failed_ || remaining_) return fail("incomplete transaction");
  active_ = false;
  if (!file_.sync()) return fail("file sync");
  if (!file_.close()) return fail("file close");
  if (!syncDirectory(paths_.current)) return fail("staging directory sync");
  if (!validate(paths_.staging, validator_, context_)) return fail("staging validation");

  const auto source = select(paths_, validator_, context_);
  if (source == PublishedFile::Unreadable) return fail("source validation");
  if (source == PublishedFile::Current) {
    // Current remains valid while retiring the older backup. Never erase the
    // only valid source to make room for a rename.
    if (Storage.exists(paths_.backup)) {
      if (!Storage.remove(paths_.backup) || !syncDirectory(paths_.current)) return fail("retire backup");
    }
    if (!Storage.rename(paths_.current, paths_.backup) || !syncDirectory(paths_.current)) return fail("retain current");
  } else if (source == PublishedFile::Backup && Storage.exists(paths_.current)) {
    if (!Storage.remove(paths_.current) || !syncDirectory(paths_.current)) return fail("retire invalid current");
  }
  if (!Storage.rename(paths_.staging, paths_.current)) return fail("publish rename");
  if (!syncDirectory(paths_.current)) return fail("publish directory sync");
  // Keep the backup. Read-only recovery needs no rename, cleanup or repair.
  return true;
}
}  // namespace booksync::storage
