// SPDX-License-Identifier: GPL-3.0-or-later
#include "ProjectionStore.h"

#include <Logging.h>

#include <cstdio>
#include <cstring>
#include <limits>
namespace booksync::storage {
namespace {
// Versioned, little-endian fixed records, with explicit fields and no C++ ABI
// padding. Aliases and transfer hints remain untrusted; never canonical IDs.
constexpr size_t FileBytes = 64 * 2 + 65 + 16 + 513 * 3 + 24 * 2 + 3 + 8;
constexpr size_t RowBytes =
    64 + 96 * 2 + 32 + 96 * 8 + 16 * 8 + FileBytes * 4 + 3 + 64 + 40 * 3 + 513 * 2 + 8 + 1 + 5 + 4;
constexpr size_t HeaderBytes = 288;
constexpr uint32_t CacheEpoch = 1;  // Durable cache domain, never a process-local account epoch.
struct Codec {
  FsFile& file;
  bool writing, ok = true;
  uint32_t hash = 2166136261u;
  void bytes(void* p, size_t n) {
    if (!ok) return;
    ok = writing ? file.write(p, n) == n : file.read(p, n) == static_cast<int>(n);
    if (ok)
      for (size_t i = 0; i < n; ++i) hash = (hash ^ static_cast<uint8_t*>(p)[i]) * 16777619u;
  }
  void number(uint64_t& v, size_t n) {
    uint8_t b[8]{};
    if (writing)
      for (size_t i = 0; i < n; ++i) b[i] = static_cast<uint8_t>(v >> (8 * i));
    bytes(b, n);
    if (!writing) {
      v = 0;
      for (size_t i = 0; i < n; ++i) v |= uint64_t(b[i]) << (8 * i);
    }
  }
  template <size_t N>
  void text(std::array<char, N>& value) {
    bytes(value.data(), value.size());
    ok = ok && std::memchr(value.data(), 0, N);
  }
  void scopeText(const char* expected) {
    if (writing) {
      bytes(const_cast<char*>(expected), 129);  // Write never modifies the caller's immutable scope.
    } else {
      char actual[129]{};  // One bounded comparison buffer, reused for both scope fields.
      bytes(actual, sizeof(actual));
      ok = ok && !std::memcmp(actual, expected, sizeof(actual));
    }
  }
  void flag(bool& v) {
    uint64_t b = v;
    number(b, 1);
    ok = ok && b <= 1;
    if (!writing) v = b;
  }
};
bool rowCodec(FsFile& file, CatalogRow& row, bool writing) {
  Codec c{file, writing};
  c.text(row.alias);
  c.text(row.title);
  c.text(row.series);
  c.text(row.sequence);
  for (auto& a : row.authors) c.text(a);
  for (auto& f : row.formats) c.text(f);
  for (auto& f : row.files) {
    c.text(f.alias);
    c.text(f.version);
    c.text(f.digest);
    c.text(f.format);
    c.text(f.destination);
    c.text(f.acquisition);
    c.text(f.rendition);
    c.text(f.requestedRendition);
    c.text(f.selection);
    c.flag(f.sourceAudio);
    c.flag(f.playableAudio);
    c.number(f.bytes, 8);
    c.flag(f.sizeKnown);
  }
  uint64_t a = row.authorCount, f = row.formatCount, n = row.fileCount;
  c.number(a, 1);
  c.number(f, 1);
  c.number(n, 1);
  c.ok = c.ok && a <= row.authors.size() && f <= row.formats.size() && n <= row.files.size();
  if (!writing) {
    row.authorCount = a;
    row.formatCount = f;
    row.fileCount = n;
  }
  c.text(row.seriesAlias);
  c.text(row.added);
  c.text(row.updated);
  c.text(row.lastOpened);
  c.text(row.artwork);
  c.text(row.detail);
  uint64_t fraction = 0;
  static_assert(sizeof(fraction) == sizeof(row.fraction));
  if (writing) std::memcpy(&fraction, &row.fraction, 8);
  c.number(fraction, 8);
  if (!writing) std::memcpy(&row.fraction, &fraction, 8);
  uint64_t reading = static_cast<uint8_t>(row.reading);
  c.number(reading, 1);
  c.ok = c.ok && reading <= static_cast<uint8_t>(CatalogReading::Finished);
  if (!writing) row.reading = static_cast<CatalogReading>(reading);
  c.flag(row.fractionKnown);
  c.flag(row.artworkKnown);
  c.flag(row.artworkPresent);
  c.flag(row.titleTruncated);
  // Reserved byte for future neutral flags; must remain zero in version 1.
  uint64_t reserved = 0;
  c.number(reserved, 1);
  c.ok = c.ok && !reserved;
  uint64_t sum = c.hash, expected = sum;
  c.number(sum, 4);
  return c.ok && (writing || sum == expected);
}
bool header(FsFile& file, uint64_t namespaceId, const char* server, const char* account, uint32_t generation,
            bool writing) {
  Codec c{file, writing};
  uint64_t magic = 0x32504342, scope = namespaceId, g = generation, bytes = RowBytes;
  uint64_t serverLength = std::strlen(server), accountLength = std::strlen(account), reserved = 0;
  c.number(magic, 4);
  c.number(scope, 8);
  c.number(g, 4);
  c.number(bytes, 4);
  c.number(serverLength, 2);
  c.number(accountLength, 2);
  c.scopeText(server);
  c.scopeText(account);
  c.number(reserved, 2);
  uint64_t sum = c.hash, expected = sum;
  c.number(sum, 4);
  return c.ok && magic == 0x32504342 && scope == namespaceId && g == generation && bytes == RowBytes &&
         serverLength == std::strlen(server) && accountLength == std::strlen(account) && !reserved &&
         (writing || sum == expected);
}
bool token(std::string_view value) { return !value.empty() && value.size() <= 128 && value.find('\0') == value.npos; }
}  // namespace
ProjectionStore::ProjectionStore(uint32_t owner, uint64_t epoch, ScopeView scope)
    : owner_(owner),
      epoch_(epoch),
      index_({paths_[0].data(), paths_[1].data(), paths_[2].data()}, paths_[3].data(), paths_[4].data(), true) {
  scopeValid_ = token(scope.server) && token(scope.account);
  if (!scopeValid_) return;
  std::memcpy(server_.data(), scope.server.data(), scope.server.size());
  std::memcpy(account_.data(), scope.account.data(), scope.account.size());
  // Only a bucket locator. Exact embedded tokens are checked before exposing
  // records or opening a writer; even a collision cannot adopt/overwrite a peer.
  namespace_ = 14695981039346656037ull;
  for (const auto* field : {server_.data(), account_.data()})
    for (size_t i = 0; i < server_.size(); ++i)
      namespace_ = (namespace_ ^ static_cast<uint8_t>(field[i])) * 1099511628211ull;
  if (!namespace_) namespace_ = 1;
  static constexpr const char* suffix[] = {"index", "index.tmp", "index.bak", "a", "b", "rows-a", "rows-b"};
  for (unsigned i = 0; i < paths_.size(); ++i)
    std::snprintf(paths_[i].data(), paths_[i].size(), "/.booksync/catalog-%016llx.%s",
                  static_cast<unsigned long long>(namespace_), suffix[i]);
}
bool ProjectionStore::accepts(ScopeView scope) const {
  return scopeValid_ && scope.server == server_.data() && scope.account == account_.data();
}
bool ProjectionStore::matches(DeliveryTicket ticket) const {
  return scopeValid_ && owner_ && epoch_ && ticket.owner == owner_ && ticket.epoch == epoch_;
}
bool ProjectionStore::recover() {
  if (!matches({owner_, epoch_})) {
    LOG_ERR("BSC", "Invalid catalog account lifetime");
    return false;
  }
  if (!index_.recover()) return false;
  if (index_.ready() && (index_.provenance().owner != namespace_ || index_.provenance().accountEpoch != CacheEpoch)) {
    LOG_ERR("BSC", "Projection scope mismatch; saved bytes retained");
    index_.retire();
    return false;
  }
  if (index_.ready() && !validCatalog()) {
    index_.retire();
    return false;
  }
  return true;
}
bool ProjectionStore::begin() {
  if (!recover()) return false;
  const auto generation = index_.ready() ? index_.provenance().generation : 0;
  if (generation == std::numeric_limits<uint32_t>::max()) {
    LOG_ERR("BSC", "Catalog generation exhausted");
    return false;
  }
  pendingGeneration_ = generation + 1;
  pendingCount_ = 0;
  failed_ = false;
  if (!index_.begin({pendingGeneration_, CacheEpoch, namespace_, 0})) return false;
  catalog_ = Storage.open(paths_[5 + (pendingGeneration_ % 2)].data(), O_WRONLY | O_CREAT | O_TRUNC);
  if (!catalog_ || !header(catalog_, namespace_, server_.data(), account_.data(), pendingGeneration_, true)) {
    LOG_ERR("BSC", "Catalog record stream open/header failed");
    cancel();
    return false;
  }
  return true;
}
bool ProjectionStore::append(const CatalogRow& row, const IndexRecord& record) {
  // Codec never changes values while writing. Avoid a second 10 KiB row copy.
  if (!catalog_ || !row.alias[0] || !rowCodec(catalog_, const_cast<CatalogRow&>(row), true) || !index_.append(record)) {
    LOG_ERR("BSC", "Catalog record publication failed");
    failed_ = true;
    return false;
  }
  ++pendingCount_;
  return true;
}
bool ProjectionStore::commit() {
  if (!catalog_ || failed_) {
    LOG_ERR("BSC", "Incomplete catalog stream cannot commit");
    cancel();
    return false;
  }
  const bool synced = catalog_.sync();
  // Inspect the length after flushing: native HAL implementations may use
  // buffered stdio, whose descriptor size can lag writes until sync().
  const bool complete = synced && catalog_.fileSize() == HeaderBytes + size_t(pendingCount_) * RowBytes;
  const bool closed = catalog_.close();
  if (!synced || !complete || !closed) {
    LOG_ERR("BSC", "Catalog stream sync/close failed");
    index_.cancel();
    return false;
  }
  // F13's manifest is the sole commit pointer for BOTH streams. Raw records
  // have reached stable storage first; no second manifest can tear independently.
  return index_.commit();
}
void ProjectionStore::cancel() {
  if (catalog_ && !catalog_.close()) LOG_ERR("BSC", "Catalog close failed");
  index_.cancel();
}
bool ProjectionStore::validCatalog() {
  const auto p = index_.provenance();
  auto file = Storage.open(paths_[5 + (p.generation % 2)].data(), O_RDONLY);
  const bool valid = file && file.fileSize() == HeaderBytes + size_t(index_.count()) * RowBytes &&
                     header(file, namespace_, server_.data(), account_.data(), p.generation, false);
  const bool closed = file.close();
  if (!valid || !closed) LOG_ERR("BSC", "Catalog stream integrity/scope failed; bytes retained");
  return valid && closed;
}
bool ProjectionStore::read(BookKey key, CatalogRow& row) {
  if (!index_.ready() || key.provenance.owner != namespace_ || key.provenance.accountEpoch != CacheEpoch ||
      key.provenance != index_.provenance() || key.ordinal >= index_.count())
    return false;
  auto file = Storage.open(paths_[5 + (key.provenance.generation % 2)].data(), O_RDONLY);
  const bool valid = file &&
                     header(file, namespace_, server_.data(), account_.data(), key.provenance.generation, false) &&
                     file.seekSet(HeaderBytes + size_t(key.ordinal) * RowBytes) && rowCodec(file, row, false);
  const bool closed = file.close();
  if (!valid || !closed) LOG_ERR("BSC", "Catalog record read/integrity failed");
  return valid && closed;
}
}  // namespace booksync::storage
