#include <HalStorage.h>
#include <uzlib.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <new>

#include "BookmarkStore.h"
#include "activities/reader/NativeReaderObserver.h"

namespace {
size_t allocationNumber = 0;
size_t failAllocation = 0;
size_t maxArrayBytes = SIZE_MAX;
bool denyLargeThrowingAllocation = false;
constexpr const char* BOOK = "/books/large.epub";
constexpr const char* MOVED = "/books/moved.epub";
}  // namespace

// Record allocations must use the nothrow array overload. Any large allocation
// through a throwing overload during a load is an immediate regression failure.
void* operator new(size_t size) {
  if (denyLargeThrowingAllocation && !bookmarkMockWriting && size >= 4096) {
    std::fprintf(stderr, "Unchecked allocation during bookmark load: %zu bytes\n", size);
    std::abort();
  }
  if (auto* bytes = std::malloc(size ? size : 1)) return bytes;
  std::abort();
}
void operator delete(void* bytes) noexcept { std::free(bytes); }
void operator delete(void* bytes, size_t) noexcept { std::free(bytes); }
void* operator new[](size_t size, const std::nothrow_t&) noexcept {
  ++allocationNumber;
  if (allocationNumber == failAllocation || size > maxArrayBytes) return nullptr;
  return std::malloc(size ? size : 1);
}
void operator delete[](void* bytes) noexcept { std::free(bytes); }
void operator delete[](void* bytes, const std::nothrow_t&) noexcept { std::free(bytes); }

namespace {
template <typename T>
void pod(std::string& bytes, const T& value) {
  bytes.append(reinterpret_cast<const char*>(&value), sizeof(value));
}
void string(std::string& bytes, const std::string& value) {
  pod(bytes, static_cast<uint32_t>(value.size()));
  bytes += value;
}
std::string storePath(const std::string& book, bool legacy = false) {
  const auto hash = legacy ? std::hash<std::string>{}(book) : uzlib_crc32(book.data(), book.size(), 0);
  return "/.crosspoint/bookmarks/epub_" + std::to_string(hash) + ".bin";
}
std::string fixture(const std::string& book, uint16_t count, uint8_t version = 5, uint16_t first = 0,
                    const std::string& title = "Title") {
  std::string bytes;
  pod(bytes, version);
  if (version == 2)
    pod(bytes, static_cast<uint8_t>(count));
  else
    pod(bytes, count);
  string(bytes, title);
  string(bytes, "Author");
  string(bytes, book);
  if (version >= 6) pod(bytes, uint64_t(5));
  for (uint16_t i = 0; i < count; ++i) {
    Bookmark bookmark{};
    bookmark.spineIndex = first + i;
    bookmark.progress = 0.375f;
    bookmark.timestamp = 1234;
    bookmark.paragraphIndex = 17;
    std::snprintf(bookmark.chapterTitle, sizeof(bookmark.chapterTitle), "Chapter %u", first + i);
    std::snprintf(bookmark.snippet, sizeof(bookmark.snippet), "Passage %u", first + i);
    pod(bytes, bookmark.spineIndex);
    pod(bytes, bookmark.progress);
    pod(bytes, bookmark.timestamp);
    bytes.append(bookmark.chapterTitle, sizeof(bookmark.chapterTitle));
    if (version >= 4) pod(bytes, bookmark.paragraphIndex);
    if (version >= 5) bytes.append(bookmark.snippet, sizeof(bookmark.snippet));
    if (version >= 6) {
      uint8_t id[16]{};
      id[0] = static_cast<uint8_t>(first + i + 1);
      id[1] = static_cast<uint8_t>((first + i + 1) >> 8);
      id[6] = 0x40;
      id[8] = 0x80;
      bytes.append(reinterpret_cast<const char*>(id), sizeof(id));
    }
  }
  return bytes;
}
void reset() {
  denyLargeThrowingAllocation = false;
  failAllocation = allocationNumber = 0;
  maxArrayBytes = SIZE_MAX;
  shortReadAfter = -1;
  failBookmarkWriteOpen = failBookmarkSync = failBookmarkClose = false;
  maxBookmarkWriteBytes = -1;
  failBookmarkRenameFrom.clear();
  BOOKMARKS.unload();
  assert(openBookmarkReaders == 0);
  Storage.files.clear();
  Storage.writes = 0;
}
bool load() {
  denyLargeThrowingAllocation = true;
  const bool ok = BOOKMARKS.loadForBook(BOOK, "Title", "Author", "epub");
  denyLargeThrowingAllocation = false;
  assert(openBookmarkReaders == 0);
  return ok;
}
void cannotOverwriteAfterFailedLoad() {
  assert(BOOKMARKS.getBookmarks().empty());
  const auto files = Storage.files;
  assert(BOOKMARKS.addBookmark(3, 0.2f, 1, "New") == BookmarkStore::AddResult::Unavailable);
  BOOKMARKS.removeBookmarkForPage(3, 0.2f, 1);
  assert(!BOOKMARKS.removeBookmarkAt(0));
  BOOKMARKS.clearAll();
  BOOKMARKS.saveToFile();
  BOOKMARKS.unload();
  assert(Storage.files == files);
  assert(Storage.writes == 0);
}
void largeLoadFailureAndRetry() {
  reset();
  const auto bytes = fixture(BOOK, 1024);
  Storage.files[storePath(BOOK)] = bytes;
  maxArrayBytes = 16 * 1024;  // Enough total records exist, but no large contiguous block.
  assert(!load());
  assert(allocationNumber == 1);
  cannotOverwriteAfterFailedLoad();
  assert(Storage.files.at(storePath(BOOK)) == bytes);
  maxArrayBytes = SIZE_MAX;
  assert(load());
  assert(BOOKMARKS.getBookmarks().size() == 1024);
  assert(BOOKMARKS.getBookmarks()[1023].spineIndex == 1023);
  assert(BOOKMARKS.getBookmarks()[1023].paragraphIndex == 17);
  assert(BOOKMARKS.getBookmarks()[1023].progress == 0.375f);
  assert(BOOKMARKS.addBookmark(2000, 0, 1, "Extra") == BookmarkStore::AddResult::LimitReached);
  assert(BOOKMARKS.removeBookmarkAt(1023));
  assert(load());
  assert(BOOKMARKS.getBookmarks().size() == 1023);
}
void versionsAndHeaderValidation() {
  for (uint8_t version : {2, 3, 4, 5}) {
    reset();
    const auto bytes = fixture(BOOK, 200, version);
    Storage.files[storePath(BOOK)] = bytes;
    failAllocation = 1;
    assert(!load());
    cannotOverwriteAfterFailedLoad();
    assert(Storage.files.at(storePath(BOOK)) == bytes);  // No partial format upgrade.
    failAllocation = 0;
    assert(load());
    const auto& bookmark = BOOKMARKS.getBookmarks()[199];
    assert(bookmark.spineIndex == 199);
    assert(bookmark.paragraphIndex == (version >= 4 ? 17 : UINT16_MAX));
    assert((bookmark.snippet[0] != '\0') == (version >= 5));
  }
  reset();
  // A large but valid unused title is streamed, never resized into a string.
  Storage.files[storePath(BOOK)] = fixture(BOOK, 1, 5, 0, std::string(32000, 'x'));
  assert(load());
  assert(BOOKMARKS.getBookmarks().size() == 1);
  for (auto bad : {std::string(), std::string("\5", 1), fixture(BOOK, 1025), fixture(MOVED, 1),
                   fixture(BOOK, 1024).substr(0, 200)}) {
    reset();
    Storage.files[storePath(BOOK)] = bad;
    assert(!load());
    assert(allocationNumber == 0);  // Reject impossible headers/sizes before allocating records.
    cannotOverwriteAfterFailedLoad();
  }
  reset();
  std::string bad;
  pod(bad, uint8_t{5});
  pod(bad, uint16_t{1});
  pod(bad, uint32_t{0xffffffff});
  Storage.files[storePath(BOOK)] = bad;
  assert(!load());
  cannotOverwriteAfterFailedLoad();
  reset();
  Storage.files[storePath(BOOK)] = fixture(BOOK, 2);
  shortReadAfter = 100;
  assert(!load());
  cannotOverwriteAfterFailedLoad();
}
void automaticRewriteFailuresPreserveSources() {
  for (bool legacyPresent : {false, true}) {
    for (int failure : {0, 1, 2, 3, 4}) {
      reset();
      Storage.files[storePath(BOOK)] = fixture(BOOK, 200, 3);
      if (legacyPresent) Storage.files[storePath(BOOK, true)] = fixture(BOOK, 8, 5, 200);
      const auto original = Storage.files;
      if (failure == 0) failBookmarkWriteOpen = true;
      if (failure == 1) maxBookmarkWriteBytes = 1;
      if (failure == 2) failBookmarkSync = true;
      if (failure == 3) failBookmarkClose = true;
      if (failure == 4) failBookmarkRenameFrom = storePath(BOOK) + ".tmp";
      assert(load());  // Read succeeded; an optional format rewrite may be retried later.
      assert(BOOKMARKS.getBookmarks().size() == (legacyPresent ? 208 : 200));
      assert(Storage.files == original);
      assert(openBookmarkReaders == 0);
    }
  }
  for (bool primaryPresent : {false, true}) {
    reset();
    Storage.files[BOOK] = "Book";
    const auto backup = fixture(BOOK, 4);
    Storage.files[storePath(BOOK) + ".bak"] = backup;
    if (primaryPresent) Storage.files[storePath(BOOK)] = fixture(BOOK, 2);
    assert(BookmarkStore::hasAnyBookmarks());
    assert(load());
    assert(BOOKMARKS.getBookmarks().size() == 4);
    assert(Storage.files.at(storePath(BOOK)) == backup);
    assert(!Storage.files.contains(storePath(BOOK) + ".bak"));
  }
  reset();
  Storage.files[BOOK] = "Book";
  Storage.files[storePath(BOOK) + ".bak"] = fixture(BOOK, 4);
  std::vector<BookmarkedBookEntry> entries;
  assert(BookmarkStore::getAllBookmarkedBooks(entries));
  assert(entries.size() == 1 && entries[0].count == 4);
  assert(Storage.files.contains(storePath(BOOK)) && !Storage.files.contains(storePath(BOOK) + ".bak"));
}

void homeHeaderScansRejectOversizedFields() {
  for (size_t titleLength : {0u, 128u, 1024u, 32000u}) {
    reset();
    Storage.files[BOOK] = "Book bytes";
    Storage.files[storePath(BOOK)] = fixture(BOOK, 1024, 5, 0, std::string(titleLength, 'x'));
    const auto original = Storage.files;
    std::vector<BookmarkedBookEntry> entries;
    denyLargeThrowingAllocation = true;
    assert(BookmarkStore::getAllBookmarkedBooks(entries));
    denyLargeThrowingAllocation = false;
    if (titleLength <= 1024) {
      assert(entries.size() == 1 && entries[0].count == 1024 && entries[0].bookPath == BOOK);
      assert(entries[0].bookTitle.size() == titleLength);
    } else {
      assert(entries.empty());
    }
    assert(Storage.files == original && Storage.writes == 0);
    assert(openBookmarkReaders == 0 && allocationNumber == 0);
  }
}

void legacyMergeFailures() {
  for (size_t failure : {1, 2, 3}) {
    reset();
    Storage.files[storePath(BOOK)] = fixture(BOOK, 8);
    Storage.files[storePath(BOOK, true)] = fixture(BOOK, 8, 5, 8);
    const auto files = Storage.files;
    failAllocation = failure;
    assert(!load());
    cannotOverwriteAfterFailedLoad();
    assert(Storage.files == files);
    failAllocation = 0;
    assert(load());
    assert(BOOKMARKS.getBookmarks().size() == 16);
    assert(!Storage.files.contains(storePath(BOOK, true)));
  }
  reset();
  Storage.files[storePath(BOOK)] = fixture(BOOK, 1024);
  Storage.files[storePath(BOOK, true)] = fixture(BOOK, 1, 5, 2000);
  const auto files = Storage.files;
  assert(!load());
  cannotOverwriteAfterFailedLoad();
  assert(Storage.files == files);
}
void addFailurePreservesLoadedRecords() {
  reset();
  Storage.files[storePath(BOOK)] = fixture(BOOK, 1);
  assert(load());
  const auto files = Storage.files;
  failAllocation = allocationNumber + 1;
  assert(BOOKMARKS.addBookmark(100, 0.2f, 1, "New") == BookmarkStore::AddResult::Unavailable);
  assert(BOOKMARKS.getBookmarks().size() == 1);
  assert(Storage.files == files);
  // Replacing a bookmark on the same page reuses its slot and needs no allocation.
  assert(BOOKMARKS.addBookmark(0, 0.375f, 10, "Replacement") == BookmarkStore::AddResult::Added);
  assert(BOOKMARKS.getBookmarks().size() == 1);
}
void recordBufferAndMigrationSuccess() {
  reset();
  BookmarkRecords records;
  Bookmark bookmark{};
  bookmark.spineIndex = 42;
  assert(records.reserve(1) && records.push_back(bookmark));
  failAllocation = allocationNumber + 1;
  assert(!records.reserve(2));
  assert(records.size() == 1 && records[0].spineIndex == 42);
  BookmarkRecords moved = std::move(records);
  assert(records.empty() && records.capacity() == 0 && moved.size() == 1);
  moved.erase(moved.begin());
  assert(moved.empty());

  for (int operation : {0, 1, 2}) {
    reset();
    Storage.files[storePath(BOOK)] = fixture(BOOK, 2);
    Storage.files[storePath(BOOK, true)] = fixture(BOOK, 2, 5, 2);
    Storage.files[storePath(MOVED)] = fixture(MOVED, 2, 5, 4);
    Storage.files[storePath(MOVED, true)] = fixture(MOVED, 2, 5, 6);
    const auto original = Storage.files;
    if (operation == 0) {
      assert(BookmarkStore::migrateForFilePath(BOOK, MOVED, "Title", "Author", "epub"));
    } else {
      BookmarkStore::RenameMigration migration;
      assert(BookmarkStore::beginRenameMigration(BOOK, MOVED, "Title", "Author", "epub", migration));
      assert(migration.active);
      if (operation == 2) {
        assert(BookmarkStore::rollbackRenameMigration(migration));
        assert(Storage.files == original);
        continue;
      }
      assert(BookmarkStore::commitRenameMigration(migration));
    }
    assert(!Storage.files.contains(storePath(BOOK)) && !Storage.files.contains(storePath(BOOK, true)));
    assert(!Storage.files.contains(storePath(MOVED, true)));
    assert(BOOKMARKS.loadForBook(MOVED, "Title", "Author", "epub"));
    assert(BOOKMARKS.getBookmarks().size() == 8);
    assert(openBookmarkReaders == 0);
  }
}

void migrationFailuresPreserveBothBooks() {
  for (bool transactional : {false, true}) {
    for (size_t failure = 1; failure <= 7; ++failure) {
      reset();
      Storage.files[storePath(BOOK)] = fixture(BOOK, 4);
      Storage.files[storePath(BOOK, true)] = fixture(BOOK, 4, 5, 4);
      Storage.files[storePath(MOVED)] = fixture(MOVED, 4, 5, 8);
      Storage.files[storePath(MOVED, true)] = fixture(MOVED, 4, 5, 12);
      const auto files = Storage.files;
      failAllocation = failure;
      if (transactional) {
        BookmarkStore::RenameMigration migration;
        assert(!BookmarkStore::beginRenameMigration(BOOK, MOVED, "Title", "Author", "epub", migration));
        assert(!migration.active);
      } else {
        assert(!BookmarkStore::migrateForFilePath(BOOK, MOVED, "Title", "Author", "epub"));
      }
      assert(openBookmarkReaders == 0);
      assert(Storage.files == files);
      assert(Storage.writes == 0);
    }
  }
}

std::vector<std::string> identities(const std::string& bytes) {
  assert(static_cast<uint8_t>(bytes[0]) == 6);
  uint16_t count;
  std::memcpy(&count, bytes.data() + 1, sizeof(count));
  size_t offset = 3;
  for (int i = 0; i < 3; ++i) {
    uint32_t size;
    assert(offset + 4 <= bytes.size());
    std::memcpy(&size, bytes.data() + offset, sizeof(size));
    offset += 4 + size;
    assert(offset <= bytes.size());
  }
  offset += 8;  // Native mutation version in v6.
  assert(offset + count * 140 == bytes.size());
  std::vector<std::string> out;
  out.reserve(count);
  for (size_t i = 0; i < count; ++i) out.push_back(bytes.substr(offset + i * 140 + 124, 16));
  return out;
}

uint64_t nativeGeneration(const std::string& bytes) {
  size_t offset = 3;
  for (int i = 0; i < 3; ++i) {
    uint32_t size;
    std::memcpy(&size, bytes.data() + offset, sizeof(size));
    offset += 4 + size;
  }
  uint64_t value;
  std::memcpy(&value, bytes.data() + offset, sizeof(value));
  return value;
}
void deletionCannotLookLikeAnOlderMissingOrEmptyNativeStore() {
  reset();
  const auto emptyBefore = fixture(BOOK, 0, 6);
  // A remotely projected row retains the native mutation version. Removing
  // that final row must advance it even though the visible state is empty.
  Storage.files[storePath(BOOK)] = fixture(BOOK, 1, 6);
  assert(load());
  assert(BOOKMARKS.removeBookmarkAt(0));
  assert(BOOKMARKS.getBookmarks().empty());
  assert(Storage.files.contains(storePath(BOOK)));
  const auto emptyAfter = Storage.files.at(storePath(BOOK));
  assert(identities(emptyAfter).empty());
  assert(nativeGeneration(emptyAfter) == nativeGeneration(emptyBefore) + 1);
  assert(emptyAfter != emptyBefore);
  assert(!BookmarkStore::hasAnyBookmarks());
  assert(load() && BOOKMARKS.getBookmarks().empty());
  assert(BOOKMARKS.addBookmark(90, 0.25f, 10, "Local") == BookmarkStore::AddResult::Added);
  assert(nativeGeneration(Storage.files.at(storePath(BOOK))) == nativeGeneration(emptyAfter) + 1);
  assert(BookmarkStore::hasAnyBookmarks());
}
void emptyMarkersDoNotConsumeTheActiveBookScanBudget() {
  reset();
  for (size_t i = 0; i < 220; ++i) {
    const auto path = std::string("/empty-") + std::to_string(i) + ".epub";
    Storage.files[storePath(path)] = fixture(path, 0, 6);
  }
  assert(!BookmarkStore::hasAnyBookmarks());
  Storage.files[BOOK] = "source";
  Storage.files[storePath(BOOK)] = fixture(BOOK, 1, 6);
  std::vector<BookmarkedBookEntry> entries;
  assert(BookmarkStore::getAllBookmarkedBooks(entries));
  assert(entries.size() == 1 && entries[0].bookPath == BOOK);
  assert(BookmarkStore::hasAnyBookmarks());
  assert(openBookmarkReaders == 0);
}
void opaqueIdentitiesSurviveNativeEditsAndRename() {
  static_assert(sizeof(Bookmark) == 128, "Identity retention must not grow the C3 record buffer");
  static uint32_t serial = 10000;
  native_reader_observer::identity = [](std::string_view kind, std::string_view book, uint8_t* id, size_t count) {
    assert(kind == "bookmarks" && book == BOOK && count == 16);
    std::memset(id, 0, count);
    const auto next = ++serial;
    std::memcpy(id, &next, sizeof(next));
    id[6] = 0x40;
    id[8] = 0x80;
    return true;
  };
  reset();
  const auto original = fixture(BOOK, 3, 6);
  const auto before = identities(original);
  Storage.files[storePath(BOOK)] = original;
  assert(load());
  assert(Storage.files.at(storePath(BOOK)) == original);
  assert(BOOKMARKS.addBookmark(90, 0.25f, 10, "New") == BookmarkStore::AddResult::Added);
  auto after = identities(Storage.files.at(storePath(BOOK)));
  assert(nativeGeneration(Storage.files.at(storePath(BOOK))) == 6);
  assert(std::equal(before.begin(), before.end(), after.begin()));
  const auto firstAdded = after.back();
  assert(firstAdded != std::string(16, '\0'));
  // Even an identical visible bookmark is a new intent after remove/re-add.
  assert(BOOKMARKS.removeBookmarkAt(3));
  assert(BOOKMARKS.addBookmark(90, 0.25f, 10, "New") == BookmarkStore::AddResult::Added);
  after = identities(Storage.files.at(storePath(BOOK)));
  assert(after.back() != firstAdded);
  assert(std::equal(before.begin(), before.end(), after.begin()));
  // A failed save/retry must not redirect offsets into a different record.
  failBookmarkSync = true;
  assert(BOOKMARKS.addBookmark(91, 0.5f, 10, "Retry") == BookmarkStore::AddResult::Added);
  assert(identities(Storage.files.at(storePath(BOOK))) == after);
  failBookmarkSync = false;
  BOOKMARKS.saveToFile();
  const auto retried = identities(Storage.files.at(storePath(BOOK)));
  assert(std::equal(after.begin(), after.end(), retried.begin()));

  // Legacy rows remain unbound; a later issuer must never stamp them retroactively.
  reset();
  Storage.files[storePath(BOOK)] = fixture(BOOK, 2, 5);
  const auto legacy = Storage.files.at(storePath(BOOK));
  assert(load());
  assert(Storage.files.at(storePath(BOOK)) == legacy);
  assert(BOOKMARKS.addBookmark(90, 0.25f, 10, "New") == BookmarkStore::AddResult::Added);
  after = identities(Storage.files.at(storePath(BOOK)));
  assert(after[0] == std::string(16, '\0') && after[1] == std::string(16, '\0'));
  assert(after[2] != std::string(16, '\0'));
  native_reader_observer::identity = nullptr;
  assert(BOOKMARKS.addBookmark(91, 0.5f, 10, "Local") == BookmarkStore::AddResult::Added);
  const auto local = identities(Storage.files.at(storePath(BOOK)));
  assert(local[2] == after[2] && local[3] == std::string(16, '\0'));

  for (int operation : {0, 1, 2}) {
    reset();
    Storage.files[storePath(BOOK)] = fixture(BOOK, 2, 6, 0);
    Storage.files[storePath(BOOK, true)] = fixture(BOOK, 2, 6, 2);
    Storage.files[storePath(MOVED)] = fixture(MOVED, 2, 6, 4);
    Storage.files[storePath(MOVED, true)] = fixture(MOVED, 2, 6, 6);
    const auto originalFiles = Storage.files;
    std::vector<std::string> expected;
    for (const auto& entry : originalFiles) {
      const auto ids = identities(entry.second);
      expected.insert(expected.end(), ids.begin(), ids.end());
    }
    std::sort(expected.begin(), expected.end());
    if (operation == 0) {
      assert(BookmarkStore::migrateForFilePath(BOOK, MOVED, "Title", "Author", "epub"));
    } else {
      BookmarkStore::RenameMigration migration;
      assert(BookmarkStore::beginRenameMigration(BOOK, MOVED, "Title", "Author", "epub", migration));
      if (operation == 2) {
        assert(BookmarkStore::rollbackRenameMigration(migration));
        assert(Storage.files == originalFiles);
        continue;
      }
      assert(BookmarkStore::commitRenameMigration(migration));
    }
    auto ids = identities(Storage.files.at(storePath(MOVED)));
    std::sort(ids.begin(), ids.end());
    assert(ids == expected && openBookmarkReaders == 0);
  }
  reset();
  Storage.files[storePath(BOOK)] = fixture(BOOK, 1024, 6);
  maxArrayBytes = 128 * 1024;
  assert(load() && allocationNumber == 1);
  assert(BOOKMARKS.getBookmarks().size() == 1024);
}
}  // namespace
int main() {
  deletionCannotLookLikeAnOlderMissingOrEmptyNativeStore();
  emptyMarkersDoNotConsumeTheActiveBookScanBudget();
  opaqueIdentitiesSurviveNativeEditsAndRename();
  largeLoadFailureAndRetry();
  versionsAndHeaderValidation();
  legacyMergeFailures();
  homeHeaderScansRejectOversizedFields();
  automaticRewriteFailuresPreserveSources();
  addFailurePreservesLoadedRecords();
  migrationFailuresPreserveBothBooks();
  recordBufferAndMigrationSuccess();
  reset();
  std::puts(
      "PASS: bookmark loading, legacy versions/merges, short reads, retry and migration survive allocation failure "
      "without changing files");
}
