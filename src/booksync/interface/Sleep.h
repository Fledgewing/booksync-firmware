// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace booksync {
// Immutable sleep-pass identity. No artwork or network handles cross Library.
// New BookSync artwork stays disabled pending device-scene approval; the retained
// native sleep renderer remains the only supported presentation.
struct SleepView {
  uint64_t generation = 0;
  bool captured = false;
};
}  // namespace booksync
