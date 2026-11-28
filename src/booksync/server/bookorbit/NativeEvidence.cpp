// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeEvidence.h"

#include <cstring>

#include "booksync/storage/SourceAuthority.h"

namespace booksync::server {
namespace {
bool digest(std::string_view hash) {
  if (hash.size() != 32) return false;
  for (const auto c : hash)
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
  return true;
}
bool text(std::string_view text) { return text.size() <= 512 && text.find('\0') == text.npos; }
uint16_t get(const char* p) { return uint8_t(p[0]) | (uint16_t(uint8_t(p[1])) << 8); }
void put(char* p, size_t n) {
  p[0] = char(n);
  p[1] = char(n >> 8);
}
}  // namespace
bool NativeEvidence::proof(std::string_view hash, std::string_view title, std::string_view authors, char* output,
                           size_t capacity, std::string_view& encoded) {
  const size_t size = 40 + title.size() + authors.size();
  if (!output || !digest(hash) || !text(title) || !text(authors) || capacity < size) return false;
  memcpy(output, "BOE1", 4);
  memcpy(output + 4, hash.data(), 32);
  put(output + 36, title.size());
  put(output + 38, authors.size());
  if (!title.empty()) memcpy(output + 40, title.data(), title.size());
  if (!authors.empty()) memcpy(output + 40 + title.size(), authors.data(), authors.size());
  encoded = {output, size};
  return true;
}
bool NativeEvidence::bindings(const storage::BindingView* rows, size_t count,
                              const storage::BookAliasBindingView* aliases, size_t aliasCount) {
  if (count > LifecycleMatch::MaxRows || aliasCount > LifecycleMatch::MaxRows || (!rows && count) ||
      (!aliases && aliasCount))
    return false;
  rows_ = rows;
  count_ = count;
  aliases_ = aliases;
  aliasCount_ = aliasCount;
  return true;
}
bool NativeEvidence::opened(const storage::JournalEntry& entry, reader::OpenCapture& open) {
  const auto current = account_.snapshot();
  if (!session_.syncReady || !current.syncReady || current.ticket.owner != session_.ticket.owner ||
      current.ticket.epoch != session_.ticket.epoch || current.syncOwner != session_.syncOwner)
    return false;
  if (entry.adapter != scope_.server || entry.owner != scope_.account ||
      !reader::Capture::decodeOpen(entry.payload, open) || !open.bound || open.binding.scope != scope_ ||
      !account_.snapshot().syncReady)
    return false;
  const auto proof = open.binding.proof;
  if (proof.size() < 40 || proof.substr(0, 4) != "BOE1" || !digest(proof.substr(4, 32))) return false;
  const size_t title = get(proof.data() + 36), authors = get(proof.data() + 38);
  return proof.size() == 40 + title + authors && text(proof.substr(40, title)) && text(proof.substr(40 + title));
}
bool NativeEvidence::load(const storage::JournalEntry& entry, MatchInput& input) {
  reader::OpenCapture open;
  if (!opened(entry, open)) return false;
  reader::Sample sample;
  bool closed = false;
  if (entry.checkpoint.substr(0, 4) != "BSC2" || !reader::Capture::decode(entry.checkpoint, sample, closed) || !closed)
    return false;
  const auto proof = open.binding.proof;
  if (proof.size() < 40 || proof.substr(0, 4) != "BOE1" || !digest(proof.substr(4, 32))) return false;
  const size_t title = get(proof.data() + 36), authors = get(proof.data() + 38);
  if (proof.size() != 40 + title + authors || !text(proof.substr(40, title)) || !text(proof.substr(40 + title)))
    return false;
  binding_ = {scope_, {}, open.path, open.binding.source};
  const auto* rows = count_ ? rows_ : &binding_;
  const auto count = count_ ? count_ : 1;
  const auto resolved = storage::resolvePath(scope_, open.path, open.binding.source.revision, rows, count);
  if (resolved.status != storage::MatchStatus::Unique || resolved.source != open.binding.source) return false;
  input = {scope_,
           session_.syncOwner,
           open.binding.source,
           open.path,
           proof.substr(4, 32),
           proof.substr(40, title),
           proof.substr(40 + title),
           sample.observedAt,
           rows,
           count,
           aliases_,
           aliasCount_};
  return true;
}
bool NativeEvidence::load(const storage::JournalEntry& entry, StatsInput& input) {
  reader::OpenCapture open;
  if (!opened(entry, open)) return false;
  // Stats runs after match, including restarts inside its own cursor envelope.
  MatchCheckpoint matched;
  if (!LifecycleMatch::decode(entry.checkpoint, matched) || matched.source != open.binding.source) return false;
  reader::Sample sample;
  bool closed = false;
  if (matched.native.substr(0, 4) != "BSC2" || !reader::Capture::decode(matched.native, sample, closed) || !closed ||
      !sample.observedAt || !pages_.available() || sample.pageEnd > pages_.count())
    return false;
  input = {scope_,           session_.syncOwner, open.binding.source,
           &pages_,          sample.nativeOpen,  sample.observedAt,
           sample.pageBegin, sample.pageEnd,     false};
  return true;
}
}  // namespace booksync::server

// Budget root only: the linker retains dormant provider/authority code in real
// firmware size reports before F46c2 schedules it. No startup calls this seam.
extern "C" bool booksync_native_evidence_link(booksync::server::NativeEvidence& evidence,
                                              const booksync::storage::JournalEntry& entry,
                                              booksync::server::MatchInput& match, booksync::server::StatsInput& stats,
                                              booksync::storage::SourceAuthority& authority,
                                              booksync::storage::ScopeView scope,
                                              const booksync::storage::Revision& revision,
                                              booksync::storage::SourceAuthority::Entropy entropy, void* entropyContext,
                                              booksync::reader::Capture& capture, char* scratch, size_t capacity) {
  booksync::storage::SourceIdentity source;
  std::string_view proof;
  const bool bound = evidence.bindings(match.rows, match.count, match.aliases, match.aliasCount);
  const bool matched = evidence.load(entry, match);
  const bool counted = evidence.load(entry, stats);
  const bool issued = authority.issue(scope, revision, entropy, entropyContext, source);
  const bool encoded = evidence.proof(match.hash, match.title, match.authors, scratch, capacity, proof);
  const auto opened = capture.open(match.path, 1, {source, scope, proof});
  return bound && matched && counted && issued && encoded && opened == booksync::storage::JournalResult::Ok;
}
