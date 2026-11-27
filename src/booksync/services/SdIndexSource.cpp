// SPDX-License-Identifier: GPL-3.0-or-later
#include "SdIndexSource.h"

#include <Logging.h>
#include <strings.h>

#include <cstring>
namespace booksync {
bool SdIndexSource::start() {
  close();
  path_[0] = '/';
  path_[1] = 0;
  lengths_[0] = 1;
  directories_[0] = Storage.open("/", O_RDONLY);
  if (!directories_[0]) {
    LOG_ERR("BSI", "Cannot open scan root");
    return false;
  }
  depth_ = 1;
  return true;
}
void SdIndexSource::close() {
  while (depth_) directories_[--depth_].close();
}
ScanStep SdIndexSource::next(void* context, storage::IndexRecord& out) {
  auto& self = *static_cast<SdIndexSource*>(context);
  if (!self.depth_) return ScanStep::End;
  auto& directory = self.directories_[self.depth_ - 1];
  auto child = directory.openNextFile();
  if (!child) {
    if (directory.allocationFailed() || directory.iterationFailed()) {
      self.close();
      return ScanStep::Error;
    }
    if (!directory.close()) {
      self.close();
      return ScanStep::Error;
    }
    --self.depth_;
    return self.depth_ ? ScanStep::Skip : ScanStep::End;
  }
  const bool folder = child.isDirectory();
  const auto n = child.getName(self.name_.data(), self.name_.size());
  const bool closed = child.close();
  if (!closed || !n || n >= self.name_.size()) {
    self.close();
    return ScanStep::Error;
  }
  if (self.name_[0] == '.') return ScanStep::Skip;
  const auto base = self.lengths_[self.depth_ - 1];
  const bool slash = base > 1;
  if (base + slash + n >= self.path_.size()) {
    self.close();
    return ScanStep::Error;
  }
  if (slash) self.path_[base] = '/';
  memcpy(self.path_.data() + base + slash, self.name_.data(), n + 1);
  if (folder) {
    if (self.depth_ == Depth) {
      self.close();
      return ScanStep::Error;
    }
    auto& next = self.directories_[self.depth_];
    next = Storage.open(self.path_.data(), O_RDONLY);
    if (!next) {
      self.close();
      return ScanStep::Error;
    }
    self.lengths_[self.depth_++] = base + slash + n;
    return ScanStep::Skip;
  }
  const char* extension = strrchr(self.name_.data(), '.');
  if (!extension || (strcasecmp(extension, ".epub") && strcasecmp(extension, ".txt") && strcasecmp(extension, ".xtc") &&
                     strcasecmp(extension, ".xtch")))
    return ScanStep::Skip;
  // No metadata parser, source digest or identity issuance is hidden in a scan.
  // Unbound local files remain explicitly unbound until F08 evidence is supplied.
  out = {};
  memcpy(out.path.data(), self.path_.data(), base + slash + n + 1);
  size_t titleLength = static_cast<size_t>(extension - self.name_.data());
  if (titleLength >= out.title.size()) {
    titleLength = out.title.size() - 1;
    while (titleLength && (static_cast<unsigned char>(self.name_[titleLength]) & 0xc0) == 0x80) --titleLength;
    out.titleTruncated = true;
  }
  if (!titleLength) return ScanStep::Skip;
  memcpy(out.title.data(), self.name_.data(), titleLength);
  return ScanStep::Record;
}
}  // namespace booksync
