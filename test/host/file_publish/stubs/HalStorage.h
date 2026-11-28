#pragma once
#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>
constexpr int O_RDONLY = 0;
constexpr int O_WRONLY = 1;
constexpr int O_CREAT = 2;
constexpr int O_TRUNC = 4;
struct PowerCut {};
struct FakeCard {
  std::map<std::string, std::string> files, disk;
  std::vector<std::string> events;
  int stop = -1, fail = -1, readOnlyAt = -1;
  int cutWrite = -1, prefix = 0, writes = 0;
  bool readOnly = false, full = false, eager = false, dead = false;
  int handles = 0, maxHandles = 0;
  size_t reportedSize = 0;
  bool event(const std::string& name) {
    events.push_back(name);
    const int n = static_cast<int>(events.size()) - 1;
    if (n == readOnlyAt) readOnly = true;
    if (n == stop) {
      dead = true;
      throw PowerCut{};
    }
    return n != fail;
  }
  void metadata() {
    if (eager) disk = files;
  }
  void restart() {
    files = disk;
    stop = fail = cutWrite = readOnlyAt = -1;
    dead = false;
    handles = 0;
    events.clear();
  }
};
inline FakeCard card;
class HalFile {
  std::string path;
  bool opened = false;
  size_t offset = 0;

 public:
  HalFile() = default;
  explicit HalFile(std::string p) : path(std::move(p)), opened(true) {
    ++card.handles;
    card.maxHandles = std::max(card.maxHandles, card.handles);
  }
  HalFile(HalFile&& other) noexcept { *this = std::move(other); }
  HalFile& operator=(HalFile&& other) noexcept {
    path = std::move(other.path);
    opened = std::exchange(other.opened, false);
    offset = other.offset;
    return *this;
  }
  explicit operator bool() const { return opened; }
  size_t write(const void* data, size_t size) {
    if (!card.event("write:before") || card.readOnly) return 0;
    const bool cut = card.writes++ == card.cutWrite;
    const size_t n = cut ? static_cast<size_t>(card.prefix) : (card.full ? size / 2 : size);
    card.files[path].append(static_cast<const char*>(data), n);
    if (card.eager) card.disk = card.files;
    if (cut) {
      card.dead = true;
      throw PowerCut{};
    }
    if (!card.event("write:after")) return 0;
    return n;
  }
  bool sync() {
    if (card.dead) return false;
    if (!card.event("file-sync:before") || card.readOnly || card.full) return false;
    card.disk[path] = card.files[path];
    return card.event("file-sync:after");
  }
  bool close() {
    if (!opened) return true;
    opened = false;
    if (card.dead) return false;
    --card.handles;
    return card.event("close");
  }
  int read(void* data, size_t size) {
    if (!card.event("read")) return -1;
    const auto& bytes = card.files.at(path);
    size = std::min(size, bytes.size() - offset);
    bytes.copy(static_cast<char*>(data), size, offset);
    offset += size;
    return static_cast<int>(size);
  }
  bool isDirectory() const { return false; }
  HalFile openNextFile() { return {}; }
  bool allocationFailed() const { return false; }
  bool iterationFailed() const { return false; }
  size_t getName(char*, size_t) { return 0; }
  bool seekSet(size_t position) {
    if (!card.event("seek") || position > fileSize()) return false;
    offset = position;
    return true;
  }
  size_t fileSize() const { return card.reportedSize ? card.reportedSize : card.files.at(path).size(); }
};
using FsFile = HalFile;
class HalStorage {
 public:
  bool mkdir(const char*) { return true; }
  bool exists(const char* path) { return card.files.count(path); }
  FsFile open(const char* path, int flags = O_RDONLY) {
    if (!card.event("open")) return {};
    if (flags & O_WRONLY) {
      if (card.readOnly || card.full) return {};
      if (flags & O_TRUNC)
        card.files[path].clear();
      else if (!exists(path) && !(flags & O_CREAT))
        return {};
      card.metadata();
    } else if (!exists(path))
      return {};
    return FsFile(path);
  }
  bool remove(const char* path) {
    if (!card.event("remove:before") || card.readOnly) return false;
    card.files.erase(path);
    card.metadata();
    return card.event("remove:after");
  }
  bool rename(const char* from, const char* to) {
    if (!card.event("rename:before") || card.readOnly || !exists(from) || exists(to)) return false;
    card.files[to] = card.files.at(from);
    card.files.erase(from);
    card.metadata();
    return card.event("rename:after");
  }
  bool syncFilesystem() {
    if (card.dead) return false;
    if (!card.event("fs-sync:before") || card.readOnly) return false;
    card.disk = card.files;
    return card.event("fs-sync:after");
  }
};
inline HalStorage Storage;
