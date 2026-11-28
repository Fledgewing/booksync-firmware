// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativePersonalSource.h"

#include <Logging.h>

#include <algorithm>
#include <cstring>
namespace booksync {
uint32_t NativePersonalSource::rankHash(std::string_view key) {
  uint32_t hash = 2166136261u;
  for (unsigned char byte : key) hash = (hash ^ byte) * 16777619u;
  return hash;
}
void NativePersonalSource::retire() {
  entries_.reset();
  count_ = 0;
  busy_ = observed_ = false;
  status_ = PersonalState::NotSaved;
}
bool NativePersonalSource::fail(PersonalState state) {
  LOG_ERR("BSP", "Native personal snapshot unavailable (%u); original captures retained", unsigned(state));
  entries_.reset();
  count_ = 0;
  busy_ = false;
  status_ = state;
  return false;
}
bool NativePersonalSource::unchanged() const {
  if (!ports_.state) return false;
  const auto now = ports_.state(ports_.context);
  return now.ready && now.provenance == frozen_.provenance && now.books == frozen_.books;
}
bool NativePersonalSource::begin() {
  retire();
  book_ = clipping_ = 0;
  if (!ports_.state || !ports_.passage) return fail(PersonalState::Unsupported);
  frozen_ = ports_.state(ports_.context);
  if (!frozen_.ready) return true;
  if (frozen_.books > MaxRows) return fail(PersonalState::Oversized);
  if (!frozen_.books) {
    status_ = PersonalState::Empty;
    return true;
  }
  busy_ = true;
  return true;
}
bool NativePersonalSource::tick(uint8_t allowance) {
  if (!busy_) return true;
  if (!allowance || allowance > 16) return false;
  if (!unchanged()) return fail(PersonalState::Unavailable);
  while (allowance-- && book_ < frozen_.books) {
    const auto step = ports_.passage(ports_.context, book_, clipping_, scratch_);
    if (step == NativePassageStep::Error) return fail(PersonalState::Unavailable);
    if (step == NativePassageStep::Oversized) return fail(PersonalState::Oversized);
    if (step == NativePassageStep::Missing || step == NativePassageStep::End) {
      observed_ = observed_ || step == NativePassageStep::End;
      ++book_;
      clipping_ = 0;
      continue;
    }
    observed_ = true;
    if (clipping_ >= 256) return fail(PersonalState::Oversized);
    if (step == NativePassageStep::Record) {
      if (!entries_) {
        // Optional captured-pool index: 80 KiB exceeds task/static storage and
        // is released on retirement. Missing/empty sources allocate no index.
        entries_ = makeUniqueNoThrow<Entry[]>(MaxRows);
        if (!entries_) return fail(PersonalState::Unavailable);
      }
      if (count_ == MaxRows) return fail(PersonalState::Oversized);
      const auto end = std::find(scratch_.identity.begin(), scratch_.identity.end(), '\0');
      if (end == scratch_.identity.begin() || end == scratch_.identity.end() || scratch_.passage.back() ||
          scratch_.source.back())
        return fail(PersonalState::Unavailable);
      entries_[count_++] = {rankHash({scratch_.identity.data(), size_t(end - scratch_.identity.begin())}), book_,
                            clipping_};
    }
    ++clipping_;
  }
  if (book_ == frozen_.books) {
    // Native unbound captures have no account rank. Stable hash order is a
    // native saved rank, with deterministic physical-coordinate tie breakers.
    if (count_ > 1)
      std::sort(entries_.get(), entries_.get() + count_, [](Entry a, Entry b) {
        if (a.hash != b.hash) return a.hash < b.hash;
        return a.book != b.book ? a.book < b.book : a.clipping < b.clipping;
      });
    busy_ = false;
    status_ = count_ ? PersonalState::Ready : observed_ ? PersonalState::Empty : PersonalState::NotSaved;
  }
  return true;
}
PersonalState NativePersonalSource::availability(PersonalKind kind) const {
  if (kind != PersonalKind::Highlight) return PersonalState::Unsupported;
  if (busy_) return PersonalState::Unavailable;
  if (status_ == PersonalState::Ready && !unchanged()) return PersonalState::Unavailable;
  return status_;
}
PersonalStep NativePersonalSource::read(PersonalKind kind, uint32_t ordinal, PersonalRecord& out) {
  if (availability(kind) != PersonalState::Ready) return PersonalStep::Error;
  if (ordinal >= count_) return PersonalStep::End;
  const auto entry = entries_[ordinal];
  if (ports_.passage(ports_.context, entry.book, entry.clipping, out) != NativePassageStep::Record)
    return PersonalStep::Error;
  const auto end = std::find(out.identity.begin(), out.identity.end(), '\0');
  if (end == out.identity.end() || rankHash({out.identity.data(), size_t(end - out.identity.begin())}) != entry.hash)
    return PersonalStep::Error;
  return PersonalStep::Record;
}
PersonalStep NativePersonalSource::find(PersonalKind kind, std::string_view identity, uint32_t& ordinal) {
  if (availability(kind) != PersonalState::Ready) return PersonalStep::Error;
  if (identity.empty() || identity.find('\0') != identity.npos || identity.size() >= scratch_.identity.size())
    return PersonalStep::Error;
  const auto hash = rankHash(identity);
  auto* end = entries_.get() + count_;
  const auto* at = std::lower_bound(entries_.get(), end, hash, [](Entry a, uint32_t h) { return a.hash < h; });
  for (; at != end && at->hash == hash; ++at) {
    const auto candidate = uint32_t(at - entries_.get());
    if (read(kind, candidate, scratch_) != PersonalStep::Record) return PersonalStep::Error;
    if (identity == scratch_.identity.data()) {
      ordinal = candidate;
      return PersonalStep::Record;
    }
  }
  return PersonalStep::End;
}
}  // namespace booksync
