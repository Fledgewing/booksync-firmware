// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/Library.h"
namespace booksync {
// Adopt outside paint. Retained native presentation is the only enabled mode;
// approved BookSync artwork can later use the same cached-only seam.
class SleepPresentation {
 public:
  auto adoptSleep(const Library& library) -> void;
  auto retainNative(uint64_t generation) const -> bool;
  auto retireSleepPresentation() -> void;
 private:
  SleepView cached_{};
};
static_assert(sizeof(SleepPresentation) <= 32);
}  // namespace booksync
