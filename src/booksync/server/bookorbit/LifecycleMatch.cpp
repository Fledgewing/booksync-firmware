// SPDX-License-Identifier: GPL-3.0-or-later
// Matching semantics ported from BookSync companion BookOrbitSync.kt, match phase.
#include "LifecycleMatch.h"

#include <Logging.h>
#include <inttypes.h>

#include <cstdio>
#include <cstring>
#include <limits>

#include "LifecycleStats.h"

namespace booksync::server {
namespace {
bool digest(std::string_view value) {
  if (value.size() != 32) return false;
  for (char c : value)
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
  return true;
}
bool text(std::string_view value, size_t max) { return value.size() <= max && value.find('\0') == value.npos; }
void put(char* out, uint64_t value, unsigned size) {
  for (unsigned i = 0; i < size; ++i) out[i] = static_cast<char>(value >> (8 * i));
}
uint64_t get(const char* in, unsigned size) {
  uint64_t value = 0;
  for (unsigned i = 0; i < size; ++i) value |= uint64_t(static_cast<uint8_t>(in[i])) << (8 * i);
  return value;
}
PhaseOutcome unavailable() {
  LOG_ERR("BSM", "Match unavailable; captured evidence and pending phase retained");
  return PhaseOutcome::Unavailable;
}
bool sameAccount(const AccountSnapshot& a, const AccountSnapshot& b) {
  return b.syncReady && a.syncOwner == b.syncOwner && a.ticket.owner == b.ticket.owner &&
         a.ticket.epoch == b.ticket.epoch;
}
}  // namespace
PhaseOutcome LifecycleMatch::run(const SyncWriterToken& token, const PhaseRequest& request) {
  if (request.phase != storage::LifecyclePhase::Match) return PhaseOutcome::Unavailable;
  const auto account = account_.snapshot();
  const auto& entry = request.entry;
  // Bounded borrowed views and a 64-byte identity; no metadata copies on stack.
  MatchInput input;
  if (!account.syncReady || !account.capabilities.sync || !request.confirmedCheckpoint || !output_ ||
      entry.checkpoint.size() > storage::LifecycleJournal::MaxCheckpoint - HeaderBytes ||
      capacity_ < HeaderBytes + entry.checkpoint.size() || !evidence_.load(entry, input) ||
      input.syncOwner != account.syncOwner || input.scope.server != entry.adapter ||
      input.scope.account != entry.owner || !input.count || input.count > MaxRows || input.aliasCount > MaxRows ||
      (!input.aliases && input.aliasCount) || !digest(input.hash) || !text(input.title, 512) ||
      !text(input.authors, 512) || input.lastOpen > uint64_t(std::numeric_limits<int64_t>::max()))
    return unavailable();
  const auto resolved = storage::resolvePath(input.scope, input.path, input.source.revision, input.rows, input.count);
  if (resolved.status != storage::MatchStatus::Unique || resolved.source != input.source) return unavailable();
  if (!sameAccount(account, account_.snapshot())) return unavailable();
  const auto reply =
      transport_.check(token, {input.hash, input.title, input.authors, input.lastOpen}, request.reconcile);
  if (!sameAccount(account, account_.snapshot())) {
    LOG_ERR("BSM", "Match account epoch changed; remote outcome retained as uncertain");
    return PhaseOutcome::Uncertain;
  }
  if (reply.result == MatchResult::Uncertain) {
    LOG_ERR("BSM", "Match outcome uncertain; reconcile before acknowledgement");
    return PhaseOutcome::Uncertain;
  }
  if (reply.result != MatchResult::Ok || reply.count > MaxRows || (!reply.rows && reply.count)) return unavailable();
  const MatchRow* found = nullptr;
  for (size_t i = 0; i < reply.count; ++i) {
    const auto& row = reply.rows[i];
    if (row.hash != input.hash) continue;
    if (!row.bookId || !row.bookFileId || row.bookId > uint64_t(std::numeric_limits<int64_t>::max()) ||
        row.bookFileId > uint64_t(std::numeric_limits<int64_t>::max()) || (found && *found != row))
      return unavailable();
    found = &row;
  }
  if (!found) return unavailable();
  char key[21];
  snprintf(key, sizeof(key), "%" PRIu64, found->bookId);
  const auto book = storage::resolveBookAlias(input.scope, {"book", key}, input.aliases, input.aliasCount);
  if (book.status != storage::MatchStatus::Missing &&
      (book.status != storage::MatchStatus::Unique || book.book != input.source.book))
    return unavailable();
  snprintf(key, sizeof(key), "%" PRIu64, found->bookFileId);
  const auto file = storage::resolveAlias(input.scope, {"book-file", key}, input.rows, input.count);
  if (file.status != storage::MatchStatus::Missing &&
      (file.status != storage::MatchStatus::Unique || file.source != input.source))
    return unavailable();
  // Preserve the complete raw capture; evidence/remote views borrow separate storage.
  if (!entry.checkpoint.empty()) std::memmove(output_ + HeaderBytes, entry.checkpoint.data(), entry.checkpoint.size());
  std::memcpy(output_, "BSM1", 4);
  std::memcpy(output_ + 4, input.source.book.bytes.data(), 16);
  std::memcpy(output_ + 20, input.source.edition.bytes.data(), 16);
  std::memcpy(output_ + 36, input.source.revision.bytes.data(), 32);
  put(output_ + 68, found->bookId, 8);
  put(output_ + 76, found->bookFileId, 8);
  output_[84] = found->positionCompatible;
  std::memcpy(output_ + 85, input.hash.data(), 32);
  put(output_ + 117, entry.checkpoint.size(), 2);
  *request.confirmedCheckpoint = {output_, HeaderBytes + entry.checkpoint.size()};
  return PhaseOutcome::Confirmed;
}
bool LifecycleMatch::decode(std::string_view bytes, MatchCheckpoint& out) {
  // Later phases still read the original verified match and raw native bytes.
  // Only one known stats envelope is allowed; unknown/nested versions stay unresolved.
  if (bytes.substr(0, 4) == "BST1") {
    constexpr size_t header = LifecycleStats::HeaderBytes;
    if (bytes.size() < header || bytes.size() > storage::LifecycleJournal::MaxCheckpoint ||
        get(bytes.data() + 32, 4) != bytes.size() - header)
      return false;
    bytes = bytes.substr(header);
  }
  if (bytes.size() < HeaderBytes || bytes.size() > storage::LifecycleJournal::MaxCheckpoint ||
      bytes.substr(0, 4) != "BSM1" || static_cast<uint8_t>(bytes[84]) > 1 ||
      get(bytes.data() + 117, 2) != bytes.size() - HeaderBytes || !digest(bytes.substr(85, 32)))
    return false;
  MatchCheckpoint value;
  std::memcpy(value.source.book.bytes.data(), bytes.data() + 4, 16);
  std::memcpy(value.source.edition.bytes.data(), bytes.data() + 20, 16);
  std::memcpy(value.source.revision.bytes.data(), bytes.data() + 36, 32);
  value.bookId = get(bytes.data() + 68, 8);
  value.bookFileId = get(bytes.data() + 76, 8);
  if (!storage::valid(value.source) || !value.bookId || !value.bookFileId ||
      value.bookId > uint64_t(std::numeric_limits<int64_t>::max()) ||
      value.bookFileId > uint64_t(std::numeric_limits<int64_t>::max()))
    return false;
  value.positionCompatible = bytes[84];
  value.hash = bytes.substr(85, 32);
  value.native = bytes.substr(HeaderBytes);
  out = value;
  return true;
}
}  // namespace booksync::server
