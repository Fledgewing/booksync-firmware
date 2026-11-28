// SPDX-License-Identifier: GPL-3.0-or-later
#include "SavedLookupPoolSource.h"

#include <cstdio>
#include <cstring>
namespace booksync {
namespace {
constexpr const char* Root = "/.crosspoint";
bool cacheName(std::string_view value) {
  if (!(value.starts_with("epub_") || value.starts_with("txt_") || value.starts_with("xtc_"))) return false;
  for (char c : value)
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-'))
      return false;
  return true;
}
}  // namespace
void SavedLookupPoolSource::begin() {
  position_ = {};
  cache_.fill(0);
  name_.fill(0);
  directories_ = bytes_ = 0;
  live_ = true;
  positioned_ = reading_ = saved_ = false;
}
LookupStep SavedLookupPoolSource::next(void* p, LookupEntry& entry) {
  return static_cast<SavedLookupPoolSource*>(p)->advance(entry);
}
LookupStep SavedLookupPoolSource::advance(LookupEntry& entry) {
  if (!live_) return LookupStep::Changed;
  if (reading_) {
    const auto before = history_.lookupConsumedBytes();
    const auto step = history_.tick(entry);
    const auto after = history_.lookupConsumedBytes();
    if (after < before || after - before > MaxCaptureBytes - bytes_) return LookupStep::Oversize;
    bytes_ += after - before;
    if (step == LookupStep::End) {
      reading_ = false;
      return history_.complete() ? LookupStep::Pending : LookupStep::Malformed;
    }
    return step;
  }
  if (!Storage.exists(Root)) return LookupStep::Missing;
  FsFile directory = Storage.open(Root, O_RDONLY);
  if (!directory) return LookupStep::IoError;
#ifdef SIMULATOR
  const bool resumed = !positioned_ || directory.seekSet(position_);
#else
  const bool resumed = !positioned_ || directory.seekDirectoryPosition(position_);
#endif
  if (!directory.isDirectory() || !resumed) {
    directory.close();
    return LookupStep::Changed;
  }
  auto child = directory.openNextFile();
  if (!child) {
    const bool failed = directory.allocationFailed() || directory.iterationFailed();
    const bool closed = directory.close();
    return failed || !closed ? LookupStep::IoError : saved_ ? LookupStep::End : LookupStep::Missing;
  }
#ifdef SIMULATOR
  position_ = directory.position();
#else
  position_ = directory.directoryPosition();
#endif
  positioned_ = true;
  const bool closedDirectory = directory.close();
  const bool folder = child.isDirectory();
  const auto n = child.getName(name_.data(), name_.size());
  const bool closedChild = child.close();
  if (!closedDirectory || !closedChild || !n || n >= name_.size()) return LookupStep::IoError;
  if (++directories_ > MaxDirectories) return LookupStep::Oversize;
  if (!folder || !cacheName({name_.data(), n})) return LookupStep::Pending;
  const auto length = std::snprintf(cache_.data(), cache_.size(), "%s/%s", Root, name_.data());
  if (length < 0 || static_cast<size_t>(length) >= cache_.size()) return LookupStep::Oversize;
  const auto step = history_.begin(cache_.data());
  if (step == LookupStep::Missing) return LookupStep::Pending;
  if (step != LookupStep::Pending) return step;
  saved_ = reading_ = true;
  return LookupStep::Pending;
}
}  // namespace booksync
// Reserve checks retain the complete dormant production capture/selection seam.
extern "C" bool booksync_lookup_pool_link(booksync::SavedLookupPoolSource* source, booksync::LookupPool* pool,
                                          int64_t day, bool begin) {
  if (!source || !pool) return false;
  if (begin) {
    source->begin();
    pool->begin(source->ports(), day);
    return pool->busy();
  }
  return pool->tick();
}
