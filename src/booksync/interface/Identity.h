// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace booksync {
struct Provenance {
  uint32_t generation = 0, accountEpoch = 1;
  uint64_t owner = 0, capture = 0;
  bool operator==(Provenance other) const {
    if (generation != other.generation) return false;
    if (accountEpoch != other.accountEpoch) return false;
    if (owner != other.owner) return false;
    return capture == other.capture;
  }
};
struct BookKey {
  Provenance provenance{};
  uint32_t ordinal = 0;
  bool operator==(BookKey other) const { return provenance == other.provenance ? ordinal == other.ordinal : false; }
};
}  // namespace booksync
