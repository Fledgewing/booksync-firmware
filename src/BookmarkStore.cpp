#include "BookmarkStore.h"

#include <AtomicFile.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>
#include <uzlib.h>

#include <algorithm>
#include <functional>
#include <limits>

#include "activities/reader/NativeReaderObserver.h"

namespace {
constexpr uint8_t LEGACY_VERSION = 2;
constexpr uint8_t COUNT_U16_VERSION = 3;
constexpr uint8_t PARAGRAPH_ANCHOR_VERSION = 4;
constexpr uint8_t SNIPPET_VERSION = 5;
constexpr uint8_t VERSION = 6;
constexpr uint32_t IDENTITY_OFFSET_MASK = 0x3fffffff;
constexpr uint32_t NEW_IDENTITY = UINT32_MAX;
constexpr size_t RECORD_BYTES = 140;
constexpr size_t IDENTITY_RECORD_OFFSET = 124;
// Stored count is uint16_t in v3+, but we keep an in-memory safety cap for ESP32-C3 RAM.
constexpr uint16_t MAX_BOOKMARKS = 1024;
constexpr char BOOKMARKS_DIR[] = "/.crosspoint/bookmarks";
constexpr char READ_FOLDER[] = "/Read";
// Header scans only need small display metadata and an SD path. Reject
// oversized fields before std::string can request a file-sized allocation.
constexpr uint32_t MAX_BOOKMARK_HEADER_FIELD_BYTES = 1024;
// Paths here are fixed directory + format + numeric hash, never full book paths.
constexpr size_t BOOKMARK_STORE_PATH_BYTES = 96;
const atomic_file::Operations BOOKMARK_FILE_OPS{
    nullptr,
    [](void*, const char* path) { return Storage.exists(path); },
    [](void*, const char* path) { return Storage.remove(path); },
    [](void*, const char* from, const char* to) { return Storage.rename(from, to); },
};

bool bookmarkSiblingPath(const std::string& path, const char* suffix, char (&out)[BOOKMARK_STORE_PATH_BYTES]) {
  const int size = snprintf(out, sizeof(out), "%s%s", path.c_str(), suffix);
  if (size >= 0 && static_cast<size_t>(size) < sizeof(out)) return true;
  LOG_ERR("BKS", "Bookmark store path too long");
  return false;
}

bool hasBookmarkFile(const std::string& path) {
  char backup[BOOKMARK_STORE_PATH_BYTES];
  return Storage.exists(path.c_str()) || (bookmarkSiblingPath(path, ".bak", backup) && Storage.exists(backup));
}

bool recoverBookmarkFile(const std::string& path) {
  char backup[BOOKMARK_STORE_PATH_BYTES];
  if (!bookmarkSiblingPath(path, ".bak", backup)) return false;
  if (atomic_file::recover(BOOKMARK_FILE_OPS, path.c_str(), backup)) return true;
  LOG_ERR("BKS", "Failed to recover bookmark backup: %s", path.c_str());
  return false;
}

std::string bookmarkScanPath(const char* name) {
  std::string path = std::string(BOOKMARKS_DIR) + "/" + name;
  if (path.ends_with(".bin.bak")) path.resize(path.size() - 4);
  return path.ends_with(".bin") ? path : std::string{};
}

// Reopen at the directory cookie so no directory/data readers overlap.
bool nextBookmarkName(uint64_t& position, char (&name)[64], bool& done) {
  done = false;
  auto directory = Storage.open(BOOKMARKS_DIR);
  if (!directory || !directory.isDirectory() || (position && !directory.seekSet(position))) {
    directory.close();
    LOG_ERR("BKS", "Cannot enumerate bookmark stores");
    return false;
  }
  auto entry = directory.openNextFile();
  if (!entry) {
    const bool failed = directory.iterationFailed() || directory.allocationFailed();
    directory.close();
    done = !failed;
    if (failed) LOG_ERR("BKS", "Bookmark directory iteration failed");
    return !failed;
  }
  position = directory.position();
  const auto length = entry.getName(name, sizeof(name));
  const bool folder = entry.isDirectory();
  entry.close();
  directory.close();
  if (folder || !length || length >= sizeof(name) - 1) name[0] = 0;
  return true;
}

struct BookmarkFileHeader {
  std::string title;
  std::string author;
  std::string path;
  std::string bookType;
  uint16_t count = 0;
};

std::string currentStoreFilePathForBook(const std::string& filePath, const std::string& bookType) {
  const uint32_t crc = uzlib_crc32(filePath.data(), static_cast<unsigned int>(filePath.size()), 0);
  return std::string(BOOKMARKS_DIR) + "/" + bookType + "_" + std::to_string(crc) + ".bin";
}

std::string legacyStoreFilePathForBook(const std::string& filePath, const std::string& bookType) {
  return std::string(BOOKMARKS_DIR) + "/" + bookType + "_" + std::to_string(std::hash<std::string>{}(filePath)) +
         ".bin";
}

bool readBookmarkCount(FsFile& file, const uint8_t version, uint16_t& count) {
  if (version == LEGACY_VERSION) {
    uint8_t legacyCount = 0;
    if (!serialization::tryReadPod(file, legacyCount)) return false;
    count = legacyCount;
    return true;
  }

  if (version == COUNT_U16_VERSION || version == PARAGRAPH_ANCHOR_VERSION || version == SNIPPET_VERSION ||
      version == VERSION) {
    return serialization::tryReadPod(file, count);
  }

  return false;
}

bool bookmarksMatchIdentity(const Bookmark& a, const Bookmark& b) {
  return a.spineIndex == b.spineIndex && a.progress == b.progress;
}

// Skip metadata that is already supplied by the caller, or compare the stored
// path in small chunks. A file length must never become a string allocation.
bool consumeStoredString(FsFile& file, const std::string* expected = nullptr) {
  uint32_t length = 0;
  if (!serialization::tryReadPod(file, length) || length > static_cast<uint32_t>(file.available()) ||
      (expected && length != expected->size()))
    return false;
  char chunk[64];
  size_t offset = 0;
  while (offset < length) {
    const size_t size = std::min(sizeof(chunk), static_cast<size_t>(length) - offset);
    if (file.read(chunk, size) != static_cast<int>(size) ||
        (expected && std::memcmp(chunk, expected->data() + offset, size) != 0))
      return false;
    offset += size;
  }
  return true;
}

bool mergeBookmarks(BookmarkRecords& dst, const BookmarkRecords& src, bool* mergedAny = nullptr) {
  size_t missing = 0;
  for (size_t i = 0; i < src.size(); ++i) {
    const auto matches = [&](const Bookmark& existing) { return bookmarksMatchIdentity(existing, src[i]); };
    if (std::none_of(dst.begin(), dst.end(), matches) && std::none_of(src.begin(), src.begin() + i, matches)) ++missing;
  }
  // Reserve the whole merge before modifying anything. Failed merges must not
  // be written back or cause a legacy/source file to be deleted.
  if (dst.size() + missing > MAX_BOOKMARKS || !dst.reserve(dst.size() + missing)) {
    LOG_ERR("BKS", "Not enough bookmark capacity to merge %u records", static_cast<unsigned>(missing));
    return false;
  }
  bool changed = false;
  for (const auto& bookmark : src) {
    if (std::any_of(dst.begin(), dst.end(),
                    [&](const Bookmark& existing) { return bookmarksMatchIdentity(existing, bookmark); }))
      continue;
    if (!dst.push_back(bookmark)) return false;  // Already reserved above.
    changed = true;
  }
  if (mergedAny) *mergedAny = changed;
  return true;
}

bool deleteBookmarkStorePath(const std::string& path, const std::string& reasonTag) {
  if (!Storage.exists(path.c_str())) {
    return true;
  }
  if (!Storage.remove(path.c_str())) {
    LOG_ERR("BKS", "Failed to delete %s bookmark file: %s", reasonTag.c_str(), path.c_str());
    return false;
  }
  return true;
}

std::string fileNameFromPath(const std::string& path) {
  const size_t lastSlash = path.rfind('/');
  return (lastSlash != std::string::npos) ? path.substr(lastSlash + 1) : path;
}

bool isInReadFolder(const std::string& path) {
  constexpr size_t n = sizeof(READ_FOLDER) - 1;
  return path.size() > n && path.compare(0, n, READ_FOLDER) == 0 && path[n] == '/';
}

bool isReadFolderCollisionVariant(const std::string& originalBase, const std::string& candidateBase) {
  if (candidateBase.size() <= originalBase.size() + 4) {
    return false;
  }
  if (candidateBase.compare(0, originalBase.size(), originalBase) != 0) {
    return false;
  }
  if (candidateBase.compare(originalBase.size(), 2, " (") != 0 || candidateBase.back() != ')') {
    return false;
  }
  for (size_t i = originalBase.size() + 2; i + 1 < candidateBase.size(); i++) {
    if (candidateBase[i] < '0' || candidateBase[i] > '9') {
      return false;
    }
  }
  return true;
}

bool resolveMovedToReadDestinationPath(const std::string& originalPath, std::string& resolvedPath) {
  const std::string fileName = fileNameFromPath(originalPath);
  if (fileName.empty() || !Storage.exists(READ_FOLDER)) {
    return false;
  }

  const std::string exactPath = std::string(READ_FOLDER) + "/" + fileName;
  if (Storage.exists(exactPath.c_str())) {
    resolvedPath = exactPath;
    return true;
  }

  const size_t dotPos = fileName.rfind('.');
  const std::string originalBase = (dotPos != std::string::npos) ? fileName.substr(0, dotPos) : fileName;
  const std::string originalExt = (dotPos != std::string::npos) ? fileName.substr(dotPos) : "";

  std::string matchedPath;
  size_t matchCount = 0;
  for (const auto& entry : Storage.listFiles(READ_FOLDER)) {
    const std::string candidateName = entry.c_str();
    const size_t candidateDotPos = candidateName.rfind('.');
    const std::string candidateBase =
        (candidateDotPos != std::string::npos) ? candidateName.substr(0, candidateDotPos) : candidateName;
    const std::string candidateExt =
        (candidateDotPos != std::string::npos) ? candidateName.substr(candidateDotPos) : "";

    if (candidateExt != originalExt || !isReadFolderCollisionVariant(originalBase, candidateBase)) {
      continue;
    }

    matchedPath = std::string(READ_FOLDER) + "/" + candidateName;
    matchCount++;
    if (matchCount > 1) {
      return false;
    }
  }

  if (matchCount == 1) {
    resolvedPath = matchedPath;
    return true;
  }
  return false;
}

bool readBookmarkHeaderString(FsFile& file, std::string& value) {
  uint32_t length = 0;
  if (!serialization::tryReadPod(file, length) || length > static_cast<uint32_t>(file.available())) return false;
  if (length > MAX_BOOKMARK_HEADER_FIELD_BYTES) {
    LOG_ERR("BKS", "Bookmark header field too large: %u bytes", length);
    return false;
  }
  value.resize(length);
  return length == 0 || file.read(value.data(), length) == static_cast<int>(length);
}

bool readBookmarkFileHeader(const std::string& fullPath, const char* name, BookmarkFileHeader& header) {
  if (!recoverBookmarkFile(fullPath)) return false;
  FsFile f;
  if (!Storage.openFileForRead("BKS", fullPath, f)) {
    return false;
  }

  if (f.available() < static_cast<int>(sizeof(uint8_t))) {
    f.close();
    return false;
  }
  uint8_t version;
  serialization::readPod(f, version);
  if (version != LEGACY_VERSION && version != COUNT_U16_VERSION && version != PARAGRAPH_ANCHOR_VERSION &&
      version != SNIPPET_VERSION && version != VERSION) {
    f.close();
    return false;
  }

  if (f.available() < static_cast<int>(version == LEGACY_VERSION ? sizeof(uint8_t) : sizeof(uint16_t))) {
    f.close();
    return false;
  }
  if (!readBookmarkCount(f, version, header.count)) {
    f.close();
    return false;
  }

  if (!readBookmarkHeaderString(f, header.title) || !readBookmarkHeaderString(f, header.author) ||
      !readBookmarkHeaderString(f, header.path)) {
    f.close();
    return false;
  }
  f.close();

  header.bookType = "epub";
  const std::string nameStr = name ? name : "";
  const size_t underscorePos = nameStr.find('_');
  if (underscorePos != std::string::npos) {
    header.bookType = nameStr.substr(0, underscorePos);
  }
  return true;
}
}  // namespace

BookmarkStore BookmarkStore::instance;

bool BookmarkStore::loadForBook(const std::string& filePath, const std::string& title, const std::string& author,
                                const std::string& bookType) {
  if (bookType != "epub" && bookType != "xtc" && bookType != "txt") {
    LOG_ERR("BKS", "Unknown book type: %s", bookType.c_str());
    return false;
  }

  bookFilePath = filePath;
  bookTitle = title;
  bookAuthor = author;
  dirty = false;
  writable = false;
  nativeGeneration_ = 0;
  identityTracked_ = false;
  bookmarks.release();

  storeFilePath = currentStoreFilePathForBook(filePath, bookType);
  const std::string legacyStoreFilePath = legacyStoreFilePathForBook(filePath, bookType);
  const bool hasCurrentFile = hasBookmarkFile(storeFilePath);
  const bool hasLegacyFile = legacyStoreFilePath != storeFilePath && hasBookmarkFile(legacyStoreFilePath);

  if (!hasCurrentFile && !hasLegacyFile) {
    if (bookType == "epub" && isInReadFolder(filePath) && Storage.exists(BOOKMARKS_DIR)) {
      for (const auto& name : Storage.listFiles(BOOKMARKS_DIR)) {
        BookmarkFileHeader header;
        const std::string fullPath = bookmarkScanPath(name.c_str());
        if (fullPath.empty()) continue;
        if (!readBookmarkFileHeader(fullPath, name.c_str(), header)) continue;
        if (header.bookType != bookType || header.count == 0 || Storage.exists(header.path.c_str())) continue;
        if (!title.empty() && !header.title.empty() && header.title != title) continue;
        if (!author.empty() && !header.author.empty() && header.author != author) continue;

        std::string resolvedMovedPath;
        if (!resolveMovedToReadDestinationPath(header.path, resolvedMovedPath) || resolvedMovedPath != filePath) {
          continue;
        }

        if (!migrateForFilePath(header.path, filePath, title, author, bookType)) return false;
        return loadForBook(filePath, title, author, bookType);
      }
    }

    writable = true;
    return true;
  }

  BookmarkRecords loadedBookmarks;
  bool loadedAny = false;
  bool needsRewrite = false;

  bool currentLoaded = false;
  if (hasCurrentFile) {
    bool currentNeedsRewrite = false;
    if (readFromFile(storeFilePath, loadedBookmarks, currentNeedsRewrite, 0, &nativeGeneration_)) {
      loadedAny = true;
      currentLoaded = true;
      needsRewrite = currentNeedsRewrite;
    } else {
      LOG_ERR("BKS", "Failed to load canonical bookmark file: %s", storeFilePath.c_str());
      return false;
    }
  }

  if (hasLegacyFile) {
    bool legacyNeedsRewrite = false;
    BookmarkRecords legacyBookmarks;
    if (!readFromFile(legacyStoreFilePath, legacyBookmarks, legacyNeedsRewrite, 1, &nativeGeneration_)) {
      LOG_ERR("BKS", "Failed to load legacy bookmark file: %s", legacyStoreFilePath.c_str());
      return false;
    }
    bool mergedLegacyBookmarks = !legacyBookmarks.empty();
    if (loadedBookmarks.empty())
      loadedBookmarks = std::move(legacyBookmarks);
    else if (!mergeBookmarks(loadedBookmarks, legacyBookmarks, &mergedLegacyBookmarks))
      return false;
    loadedAny = true;
    needsRewrite = needsRewrite || !hasCurrentFile || mergedLegacyBookmarks || legacyNeedsRewrite;
  }
  if (!loadedAny) return false;

  bookmarks = std::move(loadedBookmarks);
  identityTracked_ = nativeGeneration_ != 0 || std::any_of(bookmarks.begin(), bookmarks.end(), [](const Bookmark& row) {
                       return row.identityStorage() != 0;
                     });
  writable = true;
  if (needsRewrite) {
    dirty = true;
    saveToFile();
  }
  if (hasLegacyFile && !dirty && (currentLoaded || needsRewrite)) {
    if (deleteBookmarkStorePath(legacyStoreFilePath, "legacy")) {
      LOG_INF("BKS", "Migrated legacy bookmark store: %s -> %s", legacyStoreFilePath.c_str(), storeFilePath.c_str());
    }
  }

  return true;
}

void BookmarkStore::unload() {
  if (dirty) saveToFile();
  bookmarks.release();
  writable = false;
  bookFilePath.clear();
  bookTitle.clear();
  bookAuthor.clear();
  storeFilePath.clear();
  dirty = false;
}

BookmarkStore::AddResult BookmarkStore::addBookmark(uint16_t spineIndex, float progress, int pageCount,
                                                    const char* chapterTitle, uint16_t paragraphIndex,
                                                    const char* snippet) {
  if (!writable) {
    LOG_ERR("BKS", "Cannot add bookmarks after an unsuccessful load");
    return AddResult::Unavailable;
  }
  const auto matchesPage = [&](const Bookmark& b) {
    return pageCount > 0 && b.spineIndex == spineIndex && b.progress >= progress &&
           b.progress < progress + 1.0f / static_cast<float>(pageCount);
  };
  const size_t kept = bookmarks.size() - std::count_if(bookmarks.begin(), bookmarks.end(), matchesPage);
  if (kept >= MAX_BOOKMARKS) {
    LOG_ERR("BKS", "Bookmark limit (%u) reached", MAX_BOOKMARKS);
    return AddResult::LimitReached;
  }
  if (kept + 1 > bookmarks.capacity()) {
    const size_t capacity = std::min<size_t>(MAX_BOOKMARKS, std::max<size_t>(8, bookmarks.capacity() * 2));
    if (!bookmarks.reserve(capacity)) {
      LOG_ERR("BKS", "OOM: adding bookmark (%u records)", static_cast<unsigned>(capacity));
      return AddResult::Unavailable;
    }
  }
  for (size_t i = 0; i < bookmarks.size();) {
    if (matchesPage(bookmarks[i]))
      bookmarks.erase(bookmarks.begin() + i);
    else
      ++i;
  }

  Bookmark bm{};
  bm.spineIndex = spineIndex;
  bm.progress = progress;
  bm.timestamp = 0;  // ESP32-C3 has no battery-backed RTC; reserved for future use
  bm.identityStorage(NEW_IDENTITY);
  snprintf(bm.chapterTitle, sizeof(bm.chapterTitle), "%s", chapterTitle ? chapterTitle : "");
  bm.paragraphIndex = paragraphIndex;
  snprintf(bm.snippet, sizeof(bm.snippet), "%s", snippet ? snippet : "");

  if (!bookmarks.push_back(bm)) return AddResult::Unavailable;
  dirty = true;
  saveToFile();
  return AddResult::Added;
}

void BookmarkStore::removeBookmarkForPage(uint16_t spineIndex, float pageProgress, int pageCount) {
  if (!writable || pageCount <= 0) return;
  float pageSlice = 1.0f / static_cast<float>(pageCount);
  float pageStart = pageProgress;
  float pageEnd = pageProgress + pageSlice;

  auto it = std::find_if(bookmarks.begin(), bookmarks.end(), [&](const Bookmark& b) {
    return b.spineIndex == spineIndex && b.progress >= pageStart && b.progress < pageEnd;
  });
  if (it == bookmarks.end()) return;

  bookmarks.erase(it);
  dirty = true;
  saveToFile();
}

bool BookmarkStore::removeBookmarkAt(size_t index) {
  if (!writable || index >= bookmarks.size()) return false;

  bookmarks.erase(bookmarks.begin() + index);
  dirty = true;
  saveToFile();
  return true;
}

bool BookmarkStore::hasBookmarkForPage(uint16_t spineIndex, float pageProgress, int pageCount) {
  if (pageCount <= 0) return false;
  float pageSlice = 1.0f / static_cast<float>(pageCount);
  float pageStart = pageProgress;
  float pageEnd = pageProgress + pageSlice;

  return std::any_of(bookmarks.begin(), bookmarks.end(), [&](const Bookmark& b) {
    return b.spineIndex == spineIndex && b.progress >= pageStart && b.progress < pageEnd;
  });
}

void BookmarkStore::saveToFile() {
  if (!writable || !dirty || storeFilePath.empty()) return;
  if (bookmarks.empty()) {
    clearAll();
    return;
  }
  if (writeToFile()) dirty = false;
}

void BookmarkStore::clearAll() {
  if (!writable) {
    LOG_ERR("BKS", "Cannot clear bookmarks after an unsuccessful load");
    return;
  }
  if (!storeFilePath.empty() && (identityTracked_ || nativeGeneration_ || native_reader_observer::identity)) {
    auto previous = std::move(bookmarks);
    const bool saved = writeToFile();
    bookmarks = std::move(previous);
    if (saved) {
      bookmarks.clear();
      dirty = false;
    }
    return;
  }
  if (!storeFilePath.empty()) {
    for (const char* suffix : {"", ".bak", ".tmp"}) {
      char target[BOOKMARK_STORE_PATH_BYTES];
      if (!bookmarkSiblingPath(storeFilePath, suffix, target) || !deleteBookmarkStorePath(target, "cleared")) return;
    }
  }
  native_reader_observer::published("bookmarks", bookFilePath, storeFilePath.c_str(), true);
  bookmarks.clear();
  dirty = false;
}

bool BookmarkStore::readFromFile() {
  bool needsRewrite = false;
  if (!readFromFile(storeFilePath, bookmarks, needsRewrite, 0, &nativeGeneration_)) {
    return false;
  }

  identityTracked_ = nativeGeneration_ || std::any_of(bookmarks.begin(), bookmarks.end(),
                                                      [](const Bookmark& row) { return row.identityStorage() != 0; });
  if (needsRewrite) {
    dirty = true;
    saveToFile();
    LOG_DBG("BKS", "Migrated bookmark file to version %u", VERSION);
  }
  return true;
}

bool BookmarkStore::readFromFile(const std::string& path, BookmarkRecords& out, bool& needsRewrite,
                                 uint8_t identitySource, uint64_t* generation) const {
  if (identitySource > 3) {
    LOG_ERR("BKS", "Invalid bookmark identity source");
    return false;
  }
  needsRewrite = false;
  if (!recoverBookmarkFile(path)) return false;
  FsFile f;
  if (!Storage.openFileForRead("BKS", path, f)) {
    LOG_ERR("BKS", "Failed to open bookmark file for read: %s", path.c_str());
    return false;
  }

  uint8_t version = 0;
  if (!serialization::tryReadPod(f, version) ||
      (version != LEGACY_VERSION && version != COUNT_U16_VERSION && version != PARAGRAPH_ANCHOR_VERSION &&
       version != SNIPPET_VERSION && version != VERSION)) {
    LOG_ERR("BKS", "Invalid bookmark file version: %s", path.c_str());
    f.close();
    return false;
  }
  uint16_t count = 0;
  if (!readBookmarkCount(f, version, count) || count > MAX_BOOKMARKS || !consumeStoredString(f) ||
      !consumeStoredString(f) || !consumeStoredString(f, &bookFilePath)) {
    LOG_ERR("BKS", "Invalid bookmark header or book path: %s", path.c_str());
    f.close();
    return false;
  }
  uint64_t storedGeneration = 0;
  if (version >= VERSION && !serialization::tryReadPod(f, storedGeneration)) {
    LOG_ERR("BKS", "Truncated bookmark mutation version");
    f.close();
    return false;
  }
  constexpr size_t baseRecordBytes = sizeof(uint16_t) + sizeof(float) + sizeof(uint32_t) + BOOKMARK_CHAPTER_TITLE_MAX;
  const size_t recordBytes = baseRecordBytes + (version >= PARAGRAPH_ANCHOR_VERSION ? sizeof(uint16_t) : 0) +
                             (version >= SNIPPET_VERSION ? BOOKMARK_SNIPPET_MAX : 0) + (version >= VERSION ? 16 : 0);
  if (static_cast<size_t>(f.available()) < static_cast<size_t>(count) * recordBytes) {
    LOG_ERR("BKS", "Truncated bookmark records: %s", path.c_str());
    f.close();
    return false;
  }
  BookmarkRecords loadedBookmarks;
  if (!loadedBookmarks.reserve(count)) {
    LOG_ERR("BKS", "OOM: loading %u bookmarks (%u bytes): %s", count, static_cast<unsigned>(count * sizeof(Bookmark)),
            path.c_str());
    f.close();
    return false;
  }
  for (uint16_t i = 0; i < count; i++) {
    Bookmark bm{};
    if (!serialization::tryReadPod(f, bm.spineIndex) || !serialization::tryReadPod(f, bm.progress) ||
        !serialization::tryReadPod(f, bm.timestamp) ||
        f.read(bm.chapterTitle, sizeof(bm.chapterTitle)) != static_cast<int>(sizeof(bm.chapterTitle)) ||
        (version >= PARAGRAPH_ANCHOR_VERSION && !serialization::tryReadPod(f, bm.paragraphIndex)) ||
        (version >= SNIPPET_VERSION &&
         f.read(bm.snippet, sizeof(bm.snippet)) != static_cast<int>(sizeof(bm.snippet)))) {
      LOG_ERR("BKS", "Failed to read bookmark record %u: %s", i, path.c_str());
      f.close();
      return false;
    }
    if (version >= VERSION) {
      uint8_t id[16];
      const auto offset = f.position();
      if (offset > IDENTITY_OFFSET_MASK || f.read(id, sizeof(id)) != static_cast<int>(sizeof(id))) {
        LOG_ERR("BKS", "Invalid bookmark identity storage");
        f.close();
        return false;
      }
      if (std::any_of(std::begin(id), std::end(id), [](uint8_t byte) { return byte != 0; }))
        bm.identityStorage((uint32_t(identitySource) << 30) | static_cast<uint32_t>(offset));
    }
    bm.chapterTitle[sizeof(bm.chapterTitle) - 1] = '\0';
    bm.snippet[sizeof(bm.snippet) - 1] = '\0';
    if (!loadedBookmarks.push_back(bm)) {
      LOG_ERR("BKS", "Bookmark buffer capacity exhausted");
      f.close();
      return false;
    }
  }

  f.close();
  out = std::move(loadedBookmarks);
  if (generation) *generation = std::max(*generation, storedGeneration);
  // A v5 file remains byte-identical until an explicit native mutation. Old
  // rows acquire zero UUIDs on that write, never retroactive ownership.
  needsRewrite = version < SNIPPET_VERSION;
  return true;
}

bool BookmarkStore::writeToFile(const IdentitySources* identitySources) {
  Storage.mkdir(BOOKMARKS_DIR);
  if (!recoverBookmarkFile(storeFilePath)) return false;
  // Two bounded native paths (192 bytes) and one 16-byte UUID avoid heap
  // allocation beside a potentially 128 KiB record buffer. The backup path
  // buffer doubles as the legacy UUID-source path until its reader closes.
  // One source reader is reused across rows; no UUID array is allocated.
  char temporary[BOOKMARK_STORE_PATH_BYTES];
  char backup[BOOKMARK_STORE_PATH_BYTES];
  auto* legacy = backup;
  IdentitySources originals{};
  if (!identitySources) {
    const auto name = std::string_view(storeFilePath).substr(storeFilePath.find_last_of('/') + 1);
    const auto separator = name.find('_');
    if (separator == name.npos || separator > 16) {
      LOG_ERR("BKS", "Invalid bookmark identity path");
      return false;
    }
    const int size = snprintf(legacy, sizeof(backup), "%s/%.*s_%llu.bin", BOOKMARKS_DIR, static_cast<int>(separator),
                              name.data(), static_cast<unsigned long long>(std::hash<std::string>{}(bookFilePath)));
    if (size < 0 || static_cast<size_t>(size) >= sizeof(backup)) {
      LOG_ERR("BKS", "Bookmark identity path exceeds bound");
      return false;
    }
    originals = {storeFilePath.c_str(), legacy, nullptr, nullptr};
    identitySources = &originals;
  }
  if (!bookmarkSiblingPath(storeFilePath, ".tmp", temporary)) return false;
  FsFile f;
  if (!Storage.openFileForWrite("BKS", temporary, f)) {
    LOG_ERR("BKS", "Failed to open bookmark temporary file for write");
    Storage.remove(temporary);  // HAL may have created/truncated it before wrapper allocation failed.
    return false;
  }
  const bool tracked = identityTracked_ || nativeGeneration_ || native_reader_observer::identity;
  const uint64_t generation = tracked && nativeGeneration_ != UINT64_MAX ? nativeGeneration_ + 1 : nativeGeneration_;
  const uint16_t count = static_cast<uint16_t>(bookmarks.size());
  bool ok = serialization::tryWritePod(f, VERSION) && serialization::tryWritePod(f, count) &&
            serialization::tryWriteString(f, bookTitle) && serialization::tryWriteString(f, bookAuthor) &&
            serialization::tryWriteString(f, bookFilePath) && serialization::tryWritePod(f, generation);
  const auto firstIdentity = f.position() + IDENTITY_RECORD_OFFSET;
  ok = ok && firstIdentity + bookmarks.size() * RECORD_BYTES < IDENTITY_OFFSET_MASK;
  FsFile identityFile;
  const char* currentIdentitySource = nullptr;
  bool identityOpen = false;
  uint8_t id[16];
  for (const auto& bm : bookmarks) {
    if (!ok) break;
    std::fill(std::begin(id), std::end(id), 0);
    const auto location = bm.identityStorage();
    if (location == NEW_IDENTITY) {
      if (native_reader_observer::identity &&
          !native_reader_observer::identity("bookmarks", bookFilePath, id, sizeof(id))) {
        std::fill(std::begin(id), std::end(id), 0);
        LOG_ERR("BKS", "Bookmark identity unavailable; retained locally");
      }
    } else if (location) {
      const auto* source = (*identitySources)[location >> 30];
      if (!source || !source[0]) {
        ok = false;
        break;
      }
      if (currentIdentitySource != source) {
        if (identityOpen) {
          ok = identityFile.close();
          identityOpen = false;
          if (!ok) break;
        }
        if (!Storage.openFileForRead("BKS", source, identityFile)) {
          ok = false;
          break;
        }
        identityOpen = true;
        currentIdentitySource = source;
      }
      if (!identityFile.seekSet(location & IDENTITY_OFFSET_MASK) ||
          identityFile.read(id, sizeof(id)) != static_cast<int>(sizeof(id))) {
        ok = false;
        break;
      }
    }
    ok = serialization::tryWritePod(f, bm.spineIndex) && serialization::tryWritePod(f, bm.progress) &&
         serialization::tryWritePod(f, bm.timestamp) &&
         f.write(reinterpret_cast<const uint8_t*>(bm.chapterTitle), sizeof(bm.chapterTitle)) ==
             sizeof(bm.chapterTitle) &&
         serialization::tryWritePod(f, bm.paragraphIndex) &&
         f.write(reinterpret_cast<const uint8_t*>(bm.snippet), sizeof(bm.snippet)) == sizeof(bm.snippet) &&
         f.write(id, sizeof(id)) == sizeof(id);
  }
  if (identityOpen) ok = identityFile.close() && ok;
  // The shared path buffer is no longer borrowed by a source reader.
  ok = bookmarkSiblingPath(storeFilePath, ".bak", backup) && ok;
  const bool synced = ok && f.sync();
  const bool closed = f.close();
  if (!synced || !closed || !atomic_file::publish(BOOKMARK_FILE_OPS, storeFilePath.c_str(), temporary, backup)) {
    LOG_ERR("BKS", "Failed to publish bookmarks; previous file retained for recovery");
    Storage.remove(temporary);
    return false;
  }
  // Publication succeeded. All UUID references now point into the new current
  // file. On failure, original offsets and backups remain valid for retry.
  for (size_t i = 0; i < bookmarks.size(); ++i)
    bookmarks[i].identityStorage(static_cast<uint32_t>(firstIdentity + i * RECORD_BYTES));
  nativeGeneration_ = generation;
  identityTracked_ = tracked;
  native_reader_observer::published("bookmarks", bookFilePath, storeFilePath.c_str());
  return true;
}

void BookmarkStore::deleteForFilePath(const std::string& filePath, const std::string& bookType) {
  const std::string currentPath = currentStoreFilePathForBook(filePath, bookType);
  const std::string legacyPath = legacyStoreFilePathForBook(filePath, bookType);
  for (const auto& path : {currentPath, legacyPath}) {
    for (const char* suffix : {"", ".bak", ".tmp"}) {
      char target[BOOKMARK_STORE_PATH_BYTES];
      if (bookmarkSiblingPath(path, suffix, target)) deleteBookmarkStorePath(target, "deleted");
    }
  }
}

bool BookmarkStore::migrateForFilePath(const std::string& oldFilePath, const std::string& newFilePath,
                                       const std::string& title, const std::string& author,
                                       const std::string& bookType) {
  if (bookType != "epub" && bookType != "xtc" && bookType != "txt") {
    LOG_ERR("BKS", "Unknown book type for bookmark migration: %s", bookType.c_str());
    return false;
  }
  if (oldFilePath.empty() || newFilePath.empty() || oldFilePath == newFilePath) {
    return true;
  }

  const std::string srcCurrentPath = currentStoreFilePathForBook(oldFilePath, bookType);
  const std::string srcLegacyPath = legacyStoreFilePathForBook(oldFilePath, bookType);
  const std::string dstCurrentPath = currentStoreFilePathForBook(newFilePath, bookType);
  const std::string dstLegacyPath = legacyStoreFilePathForBook(newFilePath, bookType);

  const bool hasSrcCurrent = hasBookmarkFile(srcCurrentPath);
  const bool hasSrcLegacy = srcLegacyPath != srcCurrentPath && hasBookmarkFile(srcLegacyPath);
  if (!hasSrcCurrent && !hasSrcLegacy) {
    return true;
  }

  BookmarkStore sourceReader;
  sourceReader.bookFilePath = oldFilePath;
  BookmarkRecords migratedBookmarks;
  bool sourceNeedsRewrite = false;
  bool loadedSourceBookmarks = false;

  if (hasSrcCurrent) {
    bool currentNeedsRewrite = false;
    if (!sourceReader.readFromFile(srcCurrentPath, migratedBookmarks, currentNeedsRewrite, 0,
                                   &sourceReader.nativeGeneration_)) {
      LOG_ERR("BKS", "Failed to load source bookmark file during path migration: %s", srcCurrentPath.c_str());
      return false;
    }
    sourceNeedsRewrite = currentNeedsRewrite;
    loadedSourceBookmarks = true;
  }

  if (hasSrcLegacy) {
    bool legacyNeedsRewrite = false;
    BookmarkRecords legacyBookmarks;
    if (!sourceReader.readFromFile(srcLegacyPath, legacyBookmarks, legacyNeedsRewrite, 1,
                                   &sourceReader.nativeGeneration_)) {
      LOG_ERR("BKS", "Failed to load source legacy bookmark file during path migration: %s", srcLegacyPath.c_str());
      return false;
    }
    if (!mergeBookmarks(migratedBookmarks, legacyBookmarks)) return false;
    sourceNeedsRewrite = sourceNeedsRewrite || legacyNeedsRewrite;
    loadedSourceBookmarks = true;
  }

  if (!loadedSourceBookmarks) {
    return true;
  }

  BookmarkStore destReader;
  destReader.bookFilePath = newFilePath;
  bool hasDestBookmarks = false;

  if (hasBookmarkFile(dstCurrentPath)) {
    bool dstNeedsRewrite = false;
    BookmarkRecords existingBookmarks;
    if (!destReader.readFromFile(dstCurrentPath, existingBookmarks, dstNeedsRewrite, 2,
                                 &destReader.nativeGeneration_)) {
      LOG_ERR("BKS", "Failed to load destination bookmark file during path migration: %s", dstCurrentPath.c_str());
      return false;
    }
    if (!mergeBookmarks(existingBookmarks, migratedBookmarks)) return false;
    migratedBookmarks = std::move(existingBookmarks);
    hasDestBookmarks = true;
  }

  if (dstLegacyPath != dstCurrentPath && hasBookmarkFile(dstLegacyPath)) {
    bool dstLegacyNeedsRewrite = false;
    BookmarkRecords existingLegacyBookmarks;
    if (!destReader.readFromFile(dstLegacyPath, existingLegacyBookmarks, dstLegacyNeedsRewrite, 3,
                                 &destReader.nativeGeneration_)) {
      LOG_ERR("BKS", "Failed to load destination legacy bookmark file during path migration: %s",
              dstLegacyPath.c_str());
      return false;
    }
    if (!mergeBookmarks(existingLegacyBookmarks, migratedBookmarks)) return false;
    migratedBookmarks = std::move(existingLegacyBookmarks);
    hasDestBookmarks = true;
  }

  BookmarkStore writer;
  writer.bookFilePath = newFilePath;
  writer.bookTitle = title;
  writer.bookAuthor = author;
  writer.storeFilePath = dstCurrentPath;
  writer.bookmarks = std::move(migratedBookmarks);
  writer.nativeGeneration_ = std::max(sourceReader.nativeGeneration_, destReader.nativeGeneration_);
  writer.identityTracked_ =
      writer.nativeGeneration_ || std::any_of(writer.bookmarks.begin(), writer.bookmarks.end(),
                                              [](const Bookmark& row) { return row.identityStorage() != 0; });

  if (!writer.bookmarks.empty() || writer.identityTracked_) {
    const IdentitySources originals{srcCurrentPath.c_str(), srcLegacyPath.c_str(), dstCurrentPath.c_str(),
                                    dstLegacyPath.c_str()};
    if (!writer.writeToFile(&originals)) {
      LOG_ERR("BKS", "Failed to write migrated bookmark file: %s", dstCurrentPath.c_str());
      return false;
    }
  } else if (hasBookmarkFile(dstCurrentPath) && !deleteBookmarkStorePath(dstCurrentPath, "empty migrated")) {
    return false;
  }

  if (!deleteBookmarkStorePath(srcCurrentPath, "source")) {
    return false;
  }
  if (srcLegacyPath != srcCurrentPath && !deleteBookmarkStorePath(srcLegacyPath, "source legacy")) {
    return false;
  }
  if (dstLegacyPath != dstCurrentPath && !deleteBookmarkStorePath(dstLegacyPath, "destination legacy")) {
    return false;
  }

  LOG_INF("BKS", "Migrated bookmark path: %s -> %s (%u bookmark(s)%s%s)", oldFilePath.c_str(), newFilePath.c_str(),
          static_cast<unsigned>(writer.bookmarks.size()), hasDestBookmarks ? ", merged with destination" : "",
          sourceNeedsRewrite ? ", normalized format" : "");
  return true;
}

bool BookmarkStore::hasStoredStateForFilePath(const std::string& filePath, const std::string& bookType) {
  const auto paths = {currentStoreFilePathForBook(filePath, bookType), legacyStoreFilePathForBook(filePath, bookType)};
  return std::any_of(paths.begin(), paths.end(), [](const auto& path) {
    return hasBookmarkFile(path) || Storage.exists((path + ".rename.bak").c_str());
  });
}

bool BookmarkStore::beginRenameMigration(const std::string& oldFilePath, const std::string& newFilePath,
                                         const std::string& title, const std::string& author,
                                         const std::string& bookType, RenameMigration& migration) {
  migration = {};
  if (bookType != "epub" && bookType != "xtc" && bookType != "txt") {
    LOG_ERR("BKS", "Unknown book type for bookmark rename migration: %s", bookType.c_str());
    return false;
  }
  if (oldFilePath.empty() || newFilePath.empty() || oldFilePath == newFilePath) return true;

  migration.sourceCurrentPath = currentStoreFilePathForBook(oldFilePath, bookType);
  migration.sourceLegacyPath = legacyStoreFilePathForBook(oldFilePath, bookType);
  migration.destinationCurrentPath = currentStoreFilePathForBook(newFilePath, bookType);
  migration.destinationLegacyPath = legacyStoreFilePathForBook(newFilePath, bookType);
  migration.destinationCurrentBackupPath = migration.destinationCurrentPath + ".rename.bak";
  migration.destinationLegacyBackupPath = migration.destinationLegacyPath + ".rename.bak";

  const auto recoverInterruptedDestination = [](const std::string& path, const std::string& backupPath) {
    if (Storage.exists(path.c_str()) || !Storage.exists(backupPath.c_str())) return true;
    if (!Storage.rename(backupPath.c_str(), path.c_str())) {
      LOG_ERR("BKS", "Failed to recover interrupted bookmark rename backup: %s", backupPath.c_str());
      return false;
    }
    LOG_INF("BKS", "Recovered interrupted bookmark rename backup: %s", path.c_str());
    return true;
  };
  if (!recoverInterruptedDestination(migration.destinationCurrentPath, migration.destinationCurrentBackupPath) ||
      (migration.destinationLegacyPath != migration.destinationCurrentPath &&
       !recoverInterruptedDestination(migration.destinationLegacyPath, migration.destinationLegacyBackupPath))) {
    return false;
  }

  const bool hasSrcCurrent = hasBookmarkFile(migration.sourceCurrentPath);
  const bool hasSrcLegacy =
      migration.sourceLegacyPath != migration.sourceCurrentPath && hasBookmarkFile(migration.sourceLegacyPath);
  if (!hasSrcCurrent && !hasSrcLegacy) return true;

  if (migration.sourceCurrentPath == migration.destinationCurrentPath ||
      migration.sourceCurrentPath == migration.destinationLegacyPath ||
      migration.sourceLegacyPath == migration.destinationCurrentPath ||
      migration.sourceLegacyPath == migration.destinationLegacyPath) {
    LOG_ERR("BKS", "Bookmark storage hash collision during rename migration");
    return false;
  }

  BookmarkStore sourceReader;
  sourceReader.bookFilePath = oldFilePath;
  BookmarkRecords migratedBookmarks;
  if (hasSrcCurrent) {
    bool needsRewrite = false;
    if (!sourceReader.readFromFile(migration.sourceCurrentPath, migratedBookmarks, needsRewrite, 0,
                                   &sourceReader.nativeGeneration_)) {
      LOG_ERR("BKS", "Failed to load source bookmarks for rename: %s", migration.sourceCurrentPath.c_str());
      return false;
    }
  }
  if (hasSrcLegacy) {
    bool needsRewrite = false;
    BookmarkRecords legacyBookmarks;
    if (!sourceReader.readFromFile(migration.sourceLegacyPath, legacyBookmarks, needsRewrite, 1,
                                   &sourceReader.nativeGeneration_)) {
      LOG_ERR("BKS", "Failed to load source legacy bookmarks for rename: %s", migration.sourceLegacyPath.c_str());
      return false;
    }
    if (!mergeBookmarks(migratedBookmarks, legacyBookmarks)) return false;
  }

  BookmarkStore destinationReader;
  destinationReader.bookFilePath = newFilePath;
  if (hasBookmarkFile(migration.destinationCurrentPath)) {
    bool needsRewrite = false;
    BookmarkRecords destinationBookmarks;
    if (!destinationReader.readFromFile(migration.destinationCurrentPath, destinationBookmarks, needsRewrite, 2,
                                        &destinationReader.nativeGeneration_)) {
      LOG_ERR("BKS", "Failed to load destination bookmarks for rename: %s", migration.destinationCurrentPath.c_str());
      return false;
    }
    if (!mergeBookmarks(destinationBookmarks, migratedBookmarks)) return false;
    migratedBookmarks = std::move(destinationBookmarks);
  }
  if (migration.destinationLegacyPath != migration.destinationCurrentPath &&
      hasBookmarkFile(migration.destinationLegacyPath)) {
    bool needsRewrite = false;
    BookmarkRecords destinationLegacyBookmarks;
    if (!destinationReader.readFromFile(migration.destinationLegacyPath, destinationLegacyBookmarks, needsRewrite, 3,
                                        &destinationReader.nativeGeneration_)) {
      LOG_ERR("BKS", "Failed to load destination legacy bookmarks for rename: %s",
              migration.destinationLegacyPath.c_str());
      return false;
    }
    if (!mergeBookmarks(destinationLegacyBookmarks, migratedBookmarks)) return false;
    migratedBookmarks = std::move(destinationLegacyBookmarks);
  }

  const auto backupDestination = [](const std::string& path, const std::string& backupPath, bool& backedUp) {
    if (!Storage.exists(path.c_str())) return true;
    if (Storage.exists(backupPath.c_str()) && !Storage.remove(backupPath.c_str())) {
      LOG_ERR("BKS", "Failed to remove stale bookmark rename backup: %s", backupPath.c_str());
      return false;
    }
    if (!Storage.rename(path.c_str(), backupPath.c_str())) {
      LOG_ERR("BKS", "Failed to preserve destination bookmarks: %s", path.c_str());
      return false;
    }
    backedUp = true;
    return true;
  };

  migration.active = true;
  if (!backupDestination(migration.destinationCurrentPath, migration.destinationCurrentBackupPath,
                         migration.destinationCurrentBackedUp) ||
      (migration.destinationLegacyPath != migration.destinationCurrentPath &&
       !backupDestination(migration.destinationLegacyPath, migration.destinationLegacyBackupPath,
                          migration.destinationLegacyBackedUp))) {
    rollbackRenameMigration(migration);
    return false;
  }

  BookmarkStore writer;
  writer.bookFilePath = newFilePath;
  writer.bookTitle = title;
  writer.bookAuthor = author;
  writer.storeFilePath = migration.destinationCurrentPath;
  writer.bookmarks = std::move(migratedBookmarks);
  writer.nativeGeneration_ = std::max(sourceReader.nativeGeneration_, destinationReader.nativeGeneration_);
  writer.identityTracked_ =
      writer.nativeGeneration_ || std::any_of(writer.bookmarks.begin(), writer.bookmarks.end(),
                                              [](const Bookmark& row) { return row.identityStorage() != 0; });
  const IdentitySources originals{migration.sourceCurrentPath.c_str(), migration.sourceLegacyPath.c_str(),
                                  migration.destinationCurrentBackupPath.c_str(),
                                  migration.destinationLegacyBackupPath.c_str()};
  if ((!writer.bookmarks.empty() || writer.identityTracked_) && !writer.writeToFile(&originals)) {
    LOG_ERR("BKS", "Failed to write transactional bookmark rename: %s", migration.destinationCurrentPath.c_str());
    rollbackRenameMigration(migration);
    return false;
  }
  return true;
}

bool BookmarkStore::commitRenameMigration(RenameMigration& migration) {
  if (!migration.active) return true;
  bool ok = deleteBookmarkStorePath(migration.sourceCurrentPath, "renamed source");
  if (migration.sourceLegacyPath != migration.sourceCurrentPath) {
    ok = deleteBookmarkStorePath(migration.sourceLegacyPath, "renamed legacy source") && ok;
  }
  if (migration.destinationCurrentBackedUp) {
    ok = deleteBookmarkStorePath(migration.destinationCurrentBackupPath, "rename backup") && ok;
  }
  if (migration.destinationLegacyBackedUp) {
    ok = deleteBookmarkStorePath(migration.destinationLegacyBackupPath, "legacy rename backup") && ok;
  }
  migration.active = false;
  return ok;
}

bool BookmarkStore::rollbackRenameMigration(RenameMigration& migration) {
  if (!migration.active) return true;
  bool ok = deleteBookmarkStorePath(migration.destinationCurrentPath, "rolled-back destination");
  const auto restoreDestination = [](const std::string& backupPath, const std::string& path, const bool backedUp) {
    if (!backedUp) return true;
    if (!Storage.rename(backupPath.c_str(), path.c_str())) {
      LOG_ERR("BKS", "Failed to restore bookmark rename backup: %s", backupPath.c_str());
      return false;
    }
    return true;
  };
  ok = restoreDestination(migration.destinationCurrentBackupPath, migration.destinationCurrentPath,
                          migration.destinationCurrentBackedUp) &&
       ok;
  ok = restoreDestination(migration.destinationLegacyBackupPath, migration.destinationLegacyPath,
                          migration.destinationLegacyBackedUp) &&
       ok;
  if (ok) migration.active = false;
  return ok;
}

bool BookmarkStore::hasAnyBookmarks() {
  if (!Storage.exists(BOOKMARKS_DIR)) return false;
  uint64_t position = 0;
  for (;;) {
    char name[64]{};
    bool done = false;
    if (!nextBookmarkName(position, name, done)) return true;  // Preserve the conservative menu hint on I/O failure.
    if (done) return false;
    const auto path = bookmarkScanPath(name);
    if (path.empty()) return true;
    FsFile file;
    if (!Storage.openFileForRead("BKS", path, file)) return true;
    uint8_t version = 0;
    uint16_t count = 0;
    uint64_t generation = 0;
    const bool emptyMarker = serialization::tryReadPod(file, version) && version == VERSION &&
                             serialization::tryReadPod(file, count) && count == 0 && consumeStoredString(file) &&
                             consumeStoredString(file) && consumeStoredString(file) &&
                             serialization::tryReadPod(file, generation) && file.available() == 0;
    file.close();
    if (!emptyMarker) return true;
  }
}

bool BookmarkStore::getAllBookmarkedBooks(std::vector<BookmarkedBookEntry>& out) {
  if (!Storage.exists(BOOKMARKS_DIR)) return true;

  uint64_t position = 0;
  size_t activeFiles = 0;
  while (activeFiles < 200) {
    char name[64]{};
    bool done = false;
    if (!nextBookmarkName(position, name, done)) return false;
    if (done) break;
    const std::string fullPath = bookmarkScanPath(name);
    if (fullPath.empty()) continue;

    BookmarkFileHeader header;
    if (!readBookmarkFileHeader(fullPath, name, header)) continue;
    auto& title = header.title;
    auto& author = header.author;
    auto& path = header.path;
    auto& bookType = header.bookType;
    const uint16_t count = header.count;

    if (path.empty() || count == 0) continue;
    ++activeFiles;
    if (!Storage.exists(path.c_str())) {
      if (bookType != "epub") continue;

      std::string movedPath;
      if (!resolveMovedToReadDestinationPath(path, movedPath)) {
        continue;
      }
      if (!BookmarkStore::migrateForFilePath(path, movedPath, title, author, bookType)) {
        continue;
      }
      path = std::move(movedPath);
    }

    auto existing = std::find_if(out.begin(), out.end(), [&](const BookmarkedBookEntry& entry) {
      return entry.bookPath == path && entry.bookType == bookType;
    });
    if (existing != out.end()) {
      existing->count = std::max(existing->count, count);
      if (existing->bookTitle.empty()) existing->bookTitle = title;
      if (existing->bookAuthor.empty()) existing->bookAuthor = author;
      continue;
    }

    out.push_back({std::move(title), std::move(author), std::move(path), std::move(bookType), count});
  }

  return true;
}
