// SPDX-License-Identifier: GPL-3.0-or-later
#include "LookupPool.h"

#include <Logging.h>
#include <Memory.h>

#include <cstring>
namespace booksync {
void LookupPool::retire() {
  scratch_.reset();
  ports_ = {};
  value_.word.fill(0);
  value_.revision = 0;
  value_.total = 0;
  value_.state = LookupPoolState::NotSaved;
  used_ = count_ = 0;
  busy_ = false;
}
void LookupPool::begin(LookupPoolPorts ports, int64_t localDay) {
  retire();
  ports_ = ports;
  day_ = localDay;
  if (!ports.next || !ports.valid) {
    end(LookupPoolState::Unavailable);
    return;
  }
  value_.state = LookupPoolState::Pending;
  busy_ = true;
}
bool LookupPool::end(LookupPoolState state) {
  if (state != LookupPoolState::Ready) {
    value_.word.fill(0);
    value_.total = 0;
  }
  value_.state = state;
  value_.revision = ++sequence_;
  scratch_.reset();
  used_ = count_ = 0;
  busy_ = false;
  if (state == LookupPoolState::Unavailable || state == LookupPoolState::Oversize)
    LOG_ERR("BSW", "Saved lookup pool refused (%u)", unsigned(state));
  return true;
}
bool LookupPool::insert() {
  const auto bytes = strnlen(entry_.word.data(), entry_.word.size());
  if (!bytes || bytes == entry_.word.size()) return end(LookupPoolState::Unavailable);
  if (!scratch_) {
    scratch_ = makeUniqueNoThrow<Scratch>();
    if (!scratch_) {
      LOG_ERR("BSW", "Cannot allocate saved lookup pool (%u bytes)", unsigned(sizeof(Scratch)));
      return end(LookupPoolState::Unavailable);
    }
  }
  uint16_t lo = 0, hi = count_;
  while (lo < hi) {
    const auto middle = static_cast<uint16_t>(lo + (hi - lo) / 2);
    const auto cmp = std::strcmp(entry_.word.data(), scratch_->words.data() + scratch_->offsets[middle]);
    if (!cmp) return true;
    if (cmp < 0)
      hi = middle;
    else
      lo = middle + 1;
  }
  if (count_ == MaxWords || bytes + 1 > ArenaBytes - used_) return end(LookupPoolState::Oversize);
  for (uint16_t i = count_; i > lo; --i) scratch_->offsets[i] = scratch_->offsets[i - 1];
  scratch_->offsets[lo] = used_;
  std::memcpy(scratch_->words.data() + used_, entry_.word.data(), bytes + 1);
  used_ += static_cast<uint16_t>(bytes + 1);
  ++count_;
  return true;
}
bool LookupPool::tick() {
  if (!busy_) return true;
  if (!ports_.valid(ports_.context)) return end(LookupPoolState::Unavailable);
  const auto step = ports_.next(ports_.context, entry_);
  switch (step) {
    case LookupStep::Pending:
    case LookupStep::EmptyLine:
      return true;
    case LookupStep::Record:
      return insert();
    case LookupStep::End:
      if (!count_) return end(LookupPoolState::Empty);
      {
        const auto pick = static_cast<uint16_t>((day_ % count_ + count_) % count_);
        const auto* word = scratch_->words.data() + scratch_->offsets[pick];
        std::memcpy(value_.word.data(), word, std::strlen(word) + 1);
        value_.total = count_;
      }
      return end(LookupPoolState::Ready);
    case LookupStep::Missing:
      return end(LookupPoolState::NotSaved);
    case LookupStep::Oversize:
      return end(LookupPoolState::Oversize);
    default:
      return end(LookupPoolState::Unavailable);
  }
}
}  // namespace booksync
