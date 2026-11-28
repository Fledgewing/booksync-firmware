// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "StatsProjection.h"
namespace booksync {
void projectYear(const StatsSnapshot&, const StatsProjection&, StatsCard&, StatsYear&);
void projectMonthly(const StatsSnapshot&, const StatsProjection&, StatsCard&, StatsMonths&);
}  // namespace booksync
