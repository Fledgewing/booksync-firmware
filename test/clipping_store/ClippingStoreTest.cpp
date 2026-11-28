#include <HalStorage.h>
#include <uzlib.h>

#include <cassert>
#include <cstdlib>
#include <new>

#include "ClippingStore.h"
#include "activities/reader/NativeReaderObserver.h"
#include "activities/reader/ReadingStatsUtils.h"

namespace {
bool failArray = false;
}
void* operator new[](size_t size, const std::nothrow_t&) noexcept {
  if (std::exchange(failArray, false)) return nullptr;
  return std::malloc(size ? size : 1);
}
void operator delete[](void* bytes) noexcept { std::free(bytes); }
void operator delete[](void* bytes, const std::nothrow_t&) noexcept { std::free(bytes); }
bool ReadingStatsDate::isValid() const { return false; }
uint32_t readingStatsDayIndex(const ReadingStatsDate&) { return 0; }
void SyncPending::SessionMarker::mark(const std::string&, uint8_t) {}

namespace {
constexpr char BOOK[] = "/book.epub";
ClippingStore& store = ClippingStore::getInstance();
template <class T>
void pod(std::string& out, T value) {
  out.append(reinterpret_cast<const char*>(&value), sizeof(value));
}
void string(std::string& out, const std::string& value) {
  pod(out, static_cast<uint32_t>(value.size()));
  out += value;
}
std::string path() {
  return "/.crosspoint/clippings/epub_" + std::to_string(uzlib_crc32(BOOK, sizeof(BOOK) - 1, 0)) + ".bin";
}
std::string bytes(const std::string& name) {
  const auto& v = fake::files.at(name)->bytes;
  return {v.begin(), v.end()};
}
std::string fixture(uint8_t version = 7, uint16_t count = 2, uint32_t sourceStart = UINT32_MAX,
                    uint32_t sourceEnd = UINT32_MAX, bool scopedRecords = true) {
  std::string out;
  pod(out, version);
  pod(out, count);
  string(out, "Title");
  string(out, "Author");
  string(out, BOOK);
  if (version >= 5) {
    pod(out, uint16_t(1));
    string(out, version >= 6 ? "0123456789abcdefdeleted" : "legacy-deleted");
  }
  for (uint16_t i = 0; i < count; ++i) {
    for (uint16_t x : {i, uint16_t(0), uint16_t(0), uint16_t(1), uint16_t(0), uint16_t(2), uint16_t(3), uint16_t(0)})
      pod(out, x);
    pod(out, uint32_t(0));
    if (version >= 3) pod(out, uint32_t(123));
    if (version >= 4) pod(out, uint16_t(UINT16_MAX));
    if (version >= 7) {
      pod(out, sourceStart);
      pod(out, sourceEnd);
    }
    if (version >= 5) {
      string(out, version >= 6 && scopedRecords ? "0123456789abcdefexisting" : "legacy");
      pod(out, uint8_t(version >= 6 && scopedRecords ? 3 : 1));
    }
    out.append(CLIPPING_CHAPTER_TITLE_MAX, '\0');
    if (version == 1)
      pod(out, uint32_t(7));
    else
      pod(out, uint16_t(7));
    out += "passage";
  }
  return out;
}
bool load() { return store.loadForBook(BOOK, "Title", "Author", "epub"); }
auto add() { return store.addClipping(0, 0, 0, 1, 0, 2, 3, "Chapter", 0, "new passage", UINT16_MAX, 123); }
void reset() {
  failArray = false;
  store.unload();
  fake::reset();
}
void cannotMutate(const std::string& original) {
  assert(!store.hasClippings());
  assert(add() == ClippingStore::AddResult::SaveFailed);
  assert(!store.removeClippingAt(0));
  assert(!store.cacheResolvedLayoutRange(0, 0, 0, 1, 123));
  assert(!store.saveToFile());
  store.clearAll();
  store.unload();
  assert(bytes(path()) == original);
}
}  // namespace
int main() {
  const auto barrier = +[](const char*) { return true; };
  // Closed-file projections are exact-key transactions. Legacy records and
  // deletion queues are preserved, never adopted by matching passage text.
  {
    reset();
    fake::add(path(), fixture(7, 1, UINT32_MAX, UINT32_MAX, false));
    unsigned notifications = 0;
    static unsigned* observed = nullptr;
    observed = &notifications;
    native_reader_observer::artifact = [](std::string_view, std::string_view, const char*, bool) { ++*observed; };
    Clipping incoming{};
    incoming.sourceStart = 20;
    incoming.sourceEnd = 40;
    incoming.timestamp = 1700000000;
    const std::string key = "NP1:opaque-account-one:row-one";
    assert(ClippingStore::applyProjection(BOOK, incoming, "passage", key, false, barrier));
    assert(notifications == 0);
    assert(load() && store.clippingCount() == 2);
    assert(!store.clippingAt(0)->scoped && store.clippingAt(1)->scoped);
    const auto first = bytes(path());
    store.unload();
    assert(ClippingStore::applyProjection(BOOK, incoming, "passage", key, false, barrier));
    assert(bytes(path()) == first);
    incoming.sourceStart = 25;
    incoming.sourceEnd = 45;
    assert(ClippingStore::applyProjection(BOOK, incoming, "changed", key, false, barrier));
    assert(load() && store.clippingCount() == 2);
    std::string text;
    assert(store.readClippingText(0, text) && text == "passage");
    assert(store.readClippingText(1, text) && text == "changed");
    assert(store.clippingAt(1)->sourceStart == 25);
    store.unload();
    std::string deletion;
    bool done = false;
    assert(ClippingStore::readDeletionForUpload(BOOK, 0, deletion, done) && !done &&
           deletion == "0123456789abcdefdeleted");
    assert(ClippingStore::applyProjection(BOOK, incoming, "", key, true, barrier));
    assert(ClippingStore::applyProjection(BOOK, incoming, "", key, true, barrier));
    assert(load() && store.clippingCount() == 1 && !store.clippingAt(0)->scoped);
    assert(notifications == 0);
    store.unload();
    const auto before = bytes(path());
    failArray = true;
    assert(!ClippingStore::applyProjection(BOOK, incoming, "new", key, false, barrier));
    assert(bytes(path()) == before);
    assert(!ClippingStore::applyProjection(BOOK, incoming, "new", "", false, barrier));
    assert(!ClippingStore::applyProjection(BOOK, incoming, "new", std::string(256, 'x'), false, barrier));
    assert(!ClippingStore::applyProjection(BOOK, incoming, "new", std::string("a\0b", 3), false, barrier));
    assert(!ClippingStore::applyProjection(BOOK, incoming, "new", "0123456789abcdefexisting", false, barrier));
    assert(!ClippingStore::applyProjection(BOOK, incoming, "new", key, false, nullptr));
    for (unsigned failure : {1u, 2u, 3u}) {
      reset();
      fake::add(path(), fixture(7, 1, UINT32_MAX, UINT32_MAX, false));
      static unsigned at = 0, failAt = 0;
      at = 0;
      failAt = failure;
      const auto failBarrier = +[](const char*) { return ++at != failAt; };
      assert(!ClippingStore::applyProjection(BOOK, incoming, "new", key, false, failBarrier));
      assert(ClippingStore::applyProjection(BOOK, incoming, "new", key, false, barrier));
      assert(load() && store.clippingCount() == 2);
      store.unload();
    }
    native_reader_observer::artifact = nullptr;
    reset();
    const auto old = fixture(5, 1);
    fake::add(path(), old);
    assert(!ClippingStore::applyProjection(BOOK, incoming, "new", key, false, barrier));
    assert(bytes(path()) == old);
    reset();
  }
  // Every readable version still loads and rewrites in CrossInk's v7 format.
  for (uint8_t version = 1; version <= 7; ++version) {
    reset();
    fake::add(path(), fixture(version));
    assert(load());
    assert(store.clippingCount() == 2);
    std::string text;
    assert(store.readClippingText(1, text) && text == "passage");
    assert(add() == ClippingStore::AddResult::Added);
    store.unload();
    assert(load());
    assert(store.clippingCount() == 3);
  }
  reset();
  assert(load());
  assert(store.addClipping(0, 1, 2, 3, 0, 5, 6, "Chapter", 1, "same passage", UINT16_MAX, 123, 100, 180) ==
         ClippingStore::AddResult::Added);
  store.unload();
  assert(load());
  assert(store.clippingAt(0)->sourceStart == 100 && store.clippingAt(0)->sourceEnd == 180);
  reset();
  fake::add(path(), fixture(6));
  assert(load());
  assert(!store.cacheSourceRange(0, 20, UINT32_MAX));
  assert(store.cacheSourceRange(0, UINT32_MAX, 40));
  store.unload();
  assert(load());
  assert(store.clippingAt(0)->sourceStart == 20 && store.clippingAt(0)->sourceEnd == 40);
  reset();
  const auto invalidSource = fixture(7, 1, 40, 20);
  fake::add(path(), invalidSource);
  assert(!load());
  cannotMutate(invalidSource);
  reset();
  assert(load());
  assert(store.addClipping(0, 0, 0, 1, 0, 0, 1, "", 0, "fragment", UINT16_MAX, 123, UINT32_MAX, 0) ==
         ClippingStore::AddResult::Added);
  store.unload();
  assert(load());
  assert(clippingSourceRangeBlocked(*store.clippingAt(0)));
  assert(!store.cacheSourceRange(0, 0, 20));
  bool edgeMatch = false;
  assert(store.clippingTextMatchesEdges(*store.clippingAt(0), "frag", "ment", edgeMatch) && edgeMatch);
  assert(store.clippingTextMatchesEdges(*store.clippingAt(0), "other", "ment", edgeMatch) && !edgeMatch);
  reset();
  fake::add(path(), fixture(6));
  assert(load());
  assert(store.cacheSourceRange(0, UINT32_MAX, 0));
  assert(clippingSourceRangeBlocked(*store.clippingAt(0)));
  assert(store.clippingAt(0)->layoutSignature == 0);
  // Every truncation boundary, including a complete first record and partial second.
  for (uint8_t version = 1; version <= 7; ++version) {
    const std::string good = fixture(version);
    for (size_t cut = 0; cut < good.size(); ++cut) {
      reset();
      const auto truncated = good.substr(0, cut);
      fake::add(path(), truncated);
      assert(!load());
      cannotMutate(truncated);
    }
  }
  const std::string good = fixture();
  reset();
  std::string huge;
  pod(huge, uint8_t(4));
  pod(huge, uint16_t(1));
  pod(huge, UINT32_MAX);
  fake::add(path(), huge);
  assert(!load());
  cannotMutate(huge);
  reset();
  fake::add(path(), good);
  failArray = true;
  assert(!load());
  cannotMutate(good);
  reset();
  assert(load());
  failArray = true;
  assert(add() == ClippingStore::AddResult::SaveFailed);
  assert(!store.hasClippings());
  assert(!fake::files.contains(path()));
  assert(add() == ClippingStore::AddResult::Added);
  // Allocation failure during a rewrite leaves both disk and index intact.
  const auto saved = bytes(path());
  failArray = true;
  assert(add() == ClippingStore::AddResult::SaveFailed);
  assert(store.clippingCount() == 1 && bytes(path()) == saved);
  store.unload();
  reset();
  fake::add(path(), fixture(4, CLIPPING_MAX_PER_BOOK));
  assert(load());
  assert(add() == ClippingStore::AddResult::LimitReached);
  reset();
  fake::add(path() + ".bak", good);
  assert(load());
  assert(store.clippingCount() == 2);
  assert(fake::files.contains(path()) && !fake::files.contains(path() + ".bak"));
  reset();
  fake::add(path() + ".bak", good);
  fake::failRename = 0;
  assert(!load());
  assert(add() == ClippingStore::AddResult::SaveFailed);
  store.unload();
  assert(bytes(path() + ".bak") == good && !fake::files.contains(path()));
  reset();
  fake::add(path(), good);
  assert(load());
  assert(!store.loadForBook(BOOK, "T", "A", "xtc"));
  cannotMutate(good);
  reset();
  fake::add(path(), good);
  assert(load());
  fake::failWrite = 0;
  assert(!store.removeClippingAt(0));
  assert(store.clippingCount() == 2 && bytes(path()) == good);
  fake::failSyncPath = path() + ".tmp";
  assert(!store.removeClippingAt(0));
  assert(store.clippingCount() == 2 && bytes(path()) == good);
  fake::failClosePath = path() + ".tmp";
  assert(!store.removeClippingAt(0));
  assert(store.clippingCount() == 2 && bytes(path()) == good);
  assert(store.saveToFile());
  reset();
}
