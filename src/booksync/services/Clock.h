// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "booksync/interface/Clock.h"
namespace booksync {
// Pure worker projection. Civil fields come from the native timezone service;
// all fields are validated together before Library can publish them.
ClockStatus projectClock(ClockEvidence evidence, int64_t utcSeconds, ClockDateTime local);
}  // namespace booksync
