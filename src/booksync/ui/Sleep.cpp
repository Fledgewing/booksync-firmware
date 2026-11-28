// SPDX-License-Identifier: GPL-3.0-or-later
#include "booksync/ui/Sleep.h"
namespace booksync {
void SleepPresentation::adoptSleep(const Library& library) { cached_ = library.sleepView(); }
bool SleepPresentation::retainNative(uint64_t generation) const {
  return generation && cached_.generation == generation && cached_.captured;
}
void SleepPresentation::retireSleepPresentation() { cached_ = {}; }
}  // namespace booksync
